/******************************************************************************
 * @desc LTE MODEM STATE MACHINE 
 * 
 * This file implements a robust state machine for managing the LTE modem.
 * It handles:  
 *   - Boot sequence (power on, AT command, SIM check)  
 *  - Network attach (AT+CGATT=1)   
 *  - PDP context activation (AT+CGACT=1,1) 
 * - TCP socket opening 
 * - MQTT connection and subscription   
 * 
 * @details 
 * The state machine is designed to be resilient to modem errors and network issues.    
 * It includes:     
 *  - Retry counters for each command type (attach, PDP, TCP, MQTT)     
 * - Timeout detection for each command     
 * - Recovery mapping from error states to retry states 
 * - Unsolicited registration status handling (+CEREG URC)  
 * - Forward path for production operation (sending commands automatically)
 * - Test mode support (disabling forward path and boot sequence for unit tests)
 * - The state machine is driven by events from the LTE driver, which provides modem responses and unsolicited messages.    
 * 
 * @note This file is part of the LTE modem component and interacts closely with the LTE driver, 
 * hardware abstraction layer (lte_hw), TCP layer (lte_tcp), and MQTT layer (lte_mqtt).         
 * 
 * 
 * 
 *     
 *  
 ******************************************************************************/

#include "lte_sm.h"
#include "lte_driver.h"
#include "lte_hw.h"
#include "lte_tcp.h"
#include "lte_mqtt.h"
#include <stdio.h>
#include <string.h>
#include "lte_types.h"

/* ============================================================================
 * 1. STATE MACHINE VARIABLES
 * ============================================================================
 *
 * s_state      → current LTE state (INIT, ATTACH, PDP, etc.)
 * s_prev_state → previous LTE state (used for recovery mapping)
 * s_state_ts   → timestamp when the current state was entered
 *
 * These three variables define the “position” of the state machine.
 * Every transition updates s_prev_state and s_state_ts.
 */

static LteSmState_t s_state      = LTE_SM_INIT;
static LteSmState_t s_prev_state = LTE_SM_INVALID;
static uint32_t     s_state_ts   = 0;

// Test-mode flags (default: production enabled) */
bool sm_forward_enabled = true;
bool sm_boot_enabled    = true;
bool sm_attach_enabled  = true;

/* ============================================================================
 * 2. COMMAND TRACKING
 * ============================================================================
 *
 * s_current_cmd tracks which AT command is currently “in flight”.
 *
 * Example:
 *   - When sending AT+CGATT=1, s_current_cmd = LTE_CMD_ATTACH
 *   - When receiving "OK", the attach handler clears s_current_cmd
 *
 * This prevents sending the same command multiple times.
 */

static LteCommand_t s_current_cmd = LTE_CMD_NONE;

/* ============================================================================
 * 3. RETRY COUNTERS
 * ============================================================================
 *
 * Each command type has its own retry counter.
 * If a command fails (ERROR), the counter increments.
 * If it reaches MAX_RETRIES, the state machine enters LTE_SM_ERROR.
 *
 * retry_attach    → attach failures
 * retry_pdp       → PDP activation failures
 * retry_tcp       → TCP open failures
 * retry_mqtt_conn → MQTT connect failures
 * retry_mqtt_sub  → MQTT subscribe failures
 */

static int retry_attach    = 0;
static int retry_pdp       = 0;
static int retry_tcp       = 0;
static int retry_mqtt_conn = 0;
static int retry_mqtt_sub  = 0;

// Maximum number of retries for each command
#define MAX_RETRIES 3

/* ============================================================================
 * 5. COMMAND TIMEOUTS
 * ============================================================================
 *
 * Each command type has a timeout. If the modem does not respond within
 * the timeout window, the state machine enters LTE_SM_ERROR.
 *
 * TIMEOUT_ATTACH       → AT+CGATT=1
 * TIMEOUT_PDP          → AT+CGACT=1,1
 * TIMEOUT_TCP_OPEN     → TCP socket open
 * TIMEOUT_MQTT_CONNECT → MQTT connect
 * TIMEOUT_MQTT_SUB     → MQTT subscribe
 */

#define TIMEOUT_ATTACH       15000
#define TIMEOUT_PDP          10000
#define TIMEOUT_TCP_OPEN      8000
#define TIMEOUT_MQTT_CONNECT  8000
#define TIMEOUT_MQTT_SUB      8000

/*
* Converts a state to a string for logging purposes.    
  
*/


static const char* stateToString(LteSmState_t state) {
    switch (state) {
        case LTE_SM_IDLE: return "LTE_SM_IDLE";
        case LTE_SM_INIT: return "LTE_SM_INIT";
        case LTE_SM_POWER_ON: return "LTE_SM_POWER_ON";
        case LTE_SM_WAIT_BOOT: return "LTE_SM_WAIT_BOOT";
        case LTE_SM_CHECK_SIM: return "LTE_SM_CHECK_SIM";
        case LTE_SM_ATTACH: return "LTE_SM_ATTACH";
        case LTE_SM_PDP: return "LTE_SM_PDP";
        case LTE_SM_OPEN_SOCKET: return "LTE_SM_OPEN_SOCKET";
        case LTE_SM_MQTT_CONNECT: return "LTE_SM_MQTT_CONNECT";
        case LTE_SM_MQTT_SUBSCRIBE: return "LTE_SM_MQTT_SUBSCRIBE";
        case LTE_SM_RUN: return "LTE_SM_RUN";
        case LTE_SM_REATTACH: return "LTE_SM_REATTACH";
        case LTE_SM_REPDP: return "LTE_SM_REPDP";
        case LTE_SM_REOPEN_SOCKET: return "LTE_SM_REOPEN_SOCKET";
        case LTE_SM_MQTT_RECONNECT: return "LTE_SM_MQTT_RECONNECT";
        case LTE_SM_MQTT_RESUBSCRIBE: return "LTE_SM_MQTT_RESUBSCRIBE";
        case LTE_SM_ERROR: return "LTE_SM_ERROR";
        default: return "UNKNOWN_STATE";
    }
}

/* Return timeout for the active command */
static uint32_t get_cmd_timeout(LteCommand_t cmd)
{
    switch (cmd) {
    case LTE_CMD_ATTACH:         return TIMEOUT_ATTACH;
    case LTE_CMD_PDP:            return TIMEOUT_PDP;
    case LTE_CMD_TCP_OPEN:       return TIMEOUT_TCP_OPEN;
    case LTE_CMD_MQTT_CONNECT:   return TIMEOUT_MQTT_CONNECT;
    case LTE_CMD_MQTT_SUBSCRIBE: return TIMEOUT_MQTT_SUB;
    default:                     return 0;
    }
}


/******************************************************************************
 * RETRY LOGIC & STATE TRANSITION HELPER
 *
 * This section explains:
 *   - How retry counters work
 *   - How they interact with command handlers
 *   - How state transitions are logged and timestamped
 *   - Why this matters for both production and test mode
 ******************************************************************************/

/* ============================================================================
 * 6. RETRY INCREMENT FUNCTION
 * ============================================================================
 *
 * increment_retry(cmd)
 *
 * Called whenever a command returns "ERROR".
 * Each command type has its own retry counter.
 *
 * Example:
 *   - If AT+CGATT=1 returns ERROR:
 *         increment_retry(LTE_CMD_ATTACH)
 *
 * If the counter reaches MAX_RETRIES (3), the state machine enters LTE_SM_ERROR.
 *
 * NOTE FOR TEST MODE:
 *   Your test harness expects EXACT retry behavior.
 *   If retry counters are not reset properly, tests will fail.
 */

static int increment_retry(LteCommand_t cmd)
{
    switch (cmd) {

    case LTE_CMD_ATTACH:
        return ++retry_attach;

    case LTE_CMD_PDP:
        return ++retry_pdp;

    case LTE_CMD_TCP_OPEN:
        return ++retry_tcp;

    case LTE_CMD_MQTT_CONNECT:
        return ++retry_mqtt_conn;

    case LTE_CMD_MQTT_SUBSCRIBE:
        return ++retry_mqtt_sub;

    default:
        printf("unexpected Command:  %d\n", cmd);
        return 0;
    }
}

/* ============================================================================
 * 7. RETRY RESET FUNCTION
 * ============================================================================
 *
 * reset_retry(cmd)
 *
 * Called whenever a command returns "OK".
 * This resets the retry counter for that command.
 *
 * Example:
 *   - If PDP activation succeeds:
 *         reset_retry(LTE_CMD_PDP)
 *
 * NOTE FOR TEST MODE:
 *   Your test harness depends on retry counters being reset EXACTLY here.
 *   If retries are not reset, the state machine may incorrectly enter ERROR.
 */

static void reset_retry(LteCommand_t cmd)
{
    switch (cmd) {

    case LTE_CMD_ATTACH:
        retry_attach = 0;
        break;

    case LTE_CMD_PDP:
        retry_pdp = 0;
        break;

    case LTE_CMD_TCP_OPEN:
        retry_tcp = 0;
        break;

    case LTE_CMD_MQTT_CONNECT:
        retry_mqtt_conn = 0;
        break;

    case LTE_CMD_MQTT_SUBSCRIBE:
        retry_mqtt_sub = 0;
        break;

    default:
        printf("unexpected Command:  %d\n", cmd);
        break;
    }
}

/* ============================================================================
 * 8. STATE TRANSITION HELPER
 * ============================================================================
 *
 * lte_sm_set_state(st)
 *
 * This is the ONLY function that should change the LTE state.
 * It performs three critical tasks:
 *
 *   1. Saves the previous state (s_prev_state)
 *   2. Updates the current state (s_state)
 *   3. Records the timestamp of the transition (s_state_ts)
 *
 * The timestamp is used for timeout detection.
 *
 * NOTE FOR TEST MODE:
 *   Your test harness checks state transitions using assert_state().
 *   If transitions do not happen EXACTLY here, tests will fail.
 */

void lte_sm_set_state(LteSmState_t st)
{
    /* Save previous state */
    s_prev_state = s_state;

    /* Update current state */
    s_state = st;

    /* Record timestamp for timeout watchdog */
    s_state_ts = lte_hw_ms();

    /* Debug print for visibility */
    if (st == LTE_SM_RUN) {
        printf("STATE -> LTE_SM_RUN (prev=%s)\n", stateToString(s_prev_state));
    } else {
        printf("STATE -> %s (prev=%s)\n", stateToString(s_state), stateToString(s_prev_state));
    }
}


/******************************************************************************
 *  COMMAND HANDLERS
 *
 * These functions process modem responses ("OK", "ERROR", "+CGATT:1", etc.)
 * for each command type:
 *
 *   - Attach (AT+CGATT=1)
 *   - PDP activation (AT+CGACT=1,1)
 *   - TCP open
 *   - MQTT connect
 *   - MQTT subscribe
 *
 * Each handler:
 *   1. Checks if the handler is enabled (test mode may disable some)
 *   2. Interprets the modem response
 *   3. Clears or increments retry counters
 *   4. Clears s_current_cmd when done
 *   5. Transitions to the next LTE state
 *
 * IMPORTANT:
 *   Your tests depend on these handlers running EXACTLY as written.
 *   If any handler is disabled (especially ATTACH), your tests freeze at state 6.
 ******************************************************************************/

/* ============================================================================
 * 9. ATTACH HANDLER — AT+CGATT=1
 * ============================================================================
 *
 * This handler processes:
 *   +CGATT: 0   → not attached
 *   +CGATT: 1   → attached
 *   OK          → attach successful
 *   ERROR       → attach failed
 *
 * TEST-MODE WARNING:
 *   If sm_attach_enabled == false, THIS ENTIRE HANDLER IS SKIPPED.
 *   That means:
 *      - "+CGATT:1" is ignored
 *      - "OK" is ignored
 *      - "ERROR" is ignored
 *   And the state machine stays stuck at LTE_SM_ATTACH (state 6).
 */

static void lte_cmd_attach_handle(const char *response)
{
    
    /* Handle URC-style attach status */
    if (strncmp(response, "+CGATT:", 7) == 0) // URC: attach status
     
     {
        /* Not attached → go to REATTACH recovery state */
        if (strcmp(response, "+CGATT: 0") == 0) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_REATTACH);
            return;
        }

        /* Attached → wait for "OK" to confirm */
        if (strcmp(response, "+CGATT: 1") == 0) {
            return; /* Do nothing yet */
        }

        return;
    }

    /* Successful attach */
    if (strcmp(response, "OK") == 0) {
        reset_retry(LTE_CMD_ATTACH);
        s_current_cmd = LTE_CMD_NONE;
        lte_sm_set_state(LTE_SM_PDP);   /* Move to PDP activation */
        return;
    }

    /* Attach failed */
    if (strcmp(response, "ERROR") == 0) {
        /* Too many failures → go to ERROR state */
        if (increment_retry(LTE_CMD_ATTACH) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
        }
        /* Retry attach */
        else {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_REATTACH);                    
        }
        return;
    }
}

/* ============================================================================
 * 10. PDP HANDLER — AT+CGACT=1,1
 * ============================================================================
 *
 * Processes:
 *   OK     → PDP activated
 *   ERROR  → PDP failed
 *
 * Next states:
 *   OK     → LTE_SM_OPEN_SOCKET
 *   ERROR  → LTE_SM_REPDP (retry)
 */

static void lte_cmd_pdp_handle(const char *response)
{
    if (strcmp(response, "OK") == 0) {
        reset_retry(LTE_CMD_PDP);
        s_current_cmd = LTE_CMD_NONE;
        lte_sm_set_state(LTE_SM_OPEN_SOCKET);
        return;
    }

    if (strcmp(response, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_PDP) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_REPDP);
        }
        return;
    }
}


/* ============================================================================
 * 11. TCP OPEN HANDLER
 * ============================================================================
 *
 * Processes:
 *   OK     → TCP socket opened
 *   ERROR  → TCP open failed
 *
 * Next states:
 *   OK     → LTE_SM_MQTT_CONNECT
 *   ERROR  → LTE_SM_REOPEN_SOCKET
 */

static void lte_cmd_tcp_handle(const char *response)
{
    if (strcmp(response, "OK") == 0) {
        reset_retry(LTE_CMD_TCP_OPEN);
        s_current_cmd = LTE_CMD_NONE;
        lte_sm_set_state(LTE_SM_MQTT_CONNECT);
        return;
    }

    if (strcmp(response, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_TCP_OPEN) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_REOPEN_SOCKET);
        }
        return;
    }
}

/* ============================================================================
 * 12. MQTT CONNECT HANDLER
 * ============================================================================
 *
 * Processes:
 *   OK     → MQTT connected
 *   ERROR  → MQTT connect failed
 *
 * Next states:
 *   OK     → LTE_SM_MQTT_SUBSCRIBE
 *   ERROR  → LTE_SM_MQTT_RECONNECT
 */

static void lte_cmd_mqtt_conn_handle(const char *response)
{
    if (strcmp(response, "OK") == 0) {
        reset_retry(LTE_CMD_MQTT_CONNECT);
        s_current_cmd = LTE_CMD_NONE;
        lte_sm_set_state(LTE_SM_MQTT_SUBSCRIBE);
        return;
    }

    if (strcmp(response, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_MQTT_CONNECT) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_MQTT_RECONNECT);
        }
        return;
    }
}

/* ============================================================================
 * 13. MQTT SUBSCRIBE HANDLER
 * ============================================================================
 *
 * Processes:
 *   OK     → subscription successful
 *   ERROR  → subscription failed
 *
 * Next states:
 *   OK     → LTE_SM_RUN (final state)
 *   ERROR  → LTE_SM_MQTT_RESUBSCRIBE
 */

static void lte_cmd_mqtt_sub_handle(const char *response)
{
    if (strcmp(response, "OK") == 0) {
        reset_retry(LTE_CMD_MQTT_SUBSCRIBE);
        s_current_cmd = LTE_CMD_NONE;
        lte_sm_set_state(LTE_SM_RUN);
        return;
    }

    if (strcmp(response, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_MQTT_SUBSCRIBE) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_MQTT_RESUBSCRIBE);
        }
        return;
    }
}

/******************************************************************************
 * END OF SECTION 3
 ******************************************************************************/


/* ============================================================================
 * 14. send_cmd_once()
 * ============================================================================
 *
 * Sends an AT command ONLY if no other command is currently active.
 *
 * Prevents duplicate commands:
 *   - If s_current_cmd != NONE, the modem is already busy.
 *   - If s_current_cmd == NONE, we send the command and mark it active.
 *
 * This ensures clean, non-repeated command dispatch.
 */

static void send_cmd_once(LteCommand_t cmd, const char *at)
{
    if (s_current_cmd == LTE_CMD_NONE) {
        s_current_cmd = cmd;
        lte_driver_send_cmd(at);
        s_state_ts = lte_hw_ms();   /* Timestamp for timeout watchdog */
    }
}

/* ============================================================================
 * 15. lte_sm_init()
 * ============================================================================
 *
 * Resets the entire state machine to a clean startup condition.
 *
 * Called:
 *   - At boot
 *   - In test-mode reset_lte()
 *
 * NOTE:
 *   Your test harness depends on this to clear retries, timestamps,
 *   and s_current_cmd before each scenario.
 */

void lte_sm_init(void)
{
    s_state      = LTE_SM_INIT;
    s_prev_state = LTE_SM_INVALID;
    s_state_ts   = lte_hw_ms();
    s_current_cmd = LTE_CMD_NONE;

    retry_attach    = 0;
    retry_pdp       = 0;
    retry_tcp       = 0;
    retry_mqtt_conn = 0;
    retry_mqtt_sub  = 0;
}

/* ============================================================================
 * 16. handle_line_forward()
 * ============================================================================
 *
 * This is the PRODUCTION forward path.
 *
 * It decides what command to send NEXT based on the current LTE state.
 *
 * IMPORTANT:
 *   In TEST MODE, sm_forward_enabled = false
 *   → forward path is disabled
 *   → this function is NOT called
 *   → tests rely ONLY on injected lines
 *
 * This is EXACTLY how your test harness should work.
 */

static void handle_line_forward(const char *response)
{
    (void)response;  /* Forward path does not use modem response directly */

    switch (s_state) {

    /* ----------------------------------------------------------------------
     * INIT → POWER_ON
     * ----------------------------------------------------------------------
     *
     * Production boot sequence:
     *   - Power on modem
     *   - Send AT
     *   - Check SIM
     *   - Enable URCs
     *   - Begin attach
     *
     * In TEST MODE:
     *   sm_boot_enabled = false
     *   → ALL boot logic is skipped
     */

    case LTE_SM_INIT:
        lte_hw_power_on_sequence();
        lte_sm_set_state(LTE_SM_POWER_ON);
        printf("Modem power-on sequence initiated\n");
        break;

    /* ----------------------------------------------------------------------
     * POWER_ON → WAIT_BOOT
     * ----------------------------------------------------------------------
     */

    case LTE_SM_POWER_ON:        
        lte_driver_send_cmd("AT\r\n");
        lte_sm_set_state(LTE_SM_WAIT_BOOT);
        printf("Waiting for modem boot...\n");
        break;

    /* ----------------------------------------------------------------------
     * WAIT_BOOT → CHECK_SIM
     * ----------------------------------------------------------------------
     *
     * Wait for "OK" from AT command.
     */

    case LTE_SM_WAIT_BOOT:        

        if (lte_driver_last_line_contains("OK")) {
            printf("Modem boot complete\n");

            /* Disable echo + check SIM */
            lte_driver_send_cmd("ATE0\r\n");
            lte_driver_send_cmd("AT+CPIN?\r\n");

            printf("Checking SIM status...\n");
            lte_sm_set_state(LTE_SM_CHECK_SIM);
        }
        break;

    /* ----------------------------------------------------------------------
     * CHECK_SIM → ATTACH
     * ----------------------------------------------------------------------
     *
     * Wait for SIM READY.
     * Enable registration URCs.
     * Begin network attach.
     */

    case LTE_SM_CHECK_SIM:
        if (lte_driver_last_line_contains("+CPIN: READY")) {
            printf("SIM is ready\n");

            /* Enable URCs */
            lte_driver_send_cmd("AT+CREG=2\r\n"); // Enable CREG URC
            lte_driver_send_cmd("AT+CGREG=2\r\n"); // Enable CGREG URC
            lte_driver_send_cmd("AT+CEREG=2\r\n"); // Enable CEREG URC

            printf("URCs enabled (CREG/CGREG/CEREG)\n");

            /* Begin attach */
            send_cmd_once(LTE_CMD_ATTACH, "AT+CGATT=1\r\n");
            lte_sm_set_state(LTE_SM_ATTACH);

            printf("Attaching to network...\n");
        } 
        break;

    /* ----------------------------------------------------------------------
     * ATTACH
     * ----------------------------------------------------------------------
     *
     * In production:
     *   - Forward path does nothing here
     *   - Attach handler processes responses
     *
     * In test mode:
     *   - sm_attach_enabled MUST be true
     *   - Otherwise attach handler is skipped
     *   - Tests freeze at LTE_SM_ATTACH (state 6)
     */

    case LTE_SM_ATTACH:       
        break;

    /* ----------------------------------------------------------------------
     * PDP ACTIVATION
     * ----------------------------------------------------------------------
     */

    case LTE_SM_PDP:
        send_cmd_once(LTE_CMD_PDP, "AT+CGACT=1,1\r\n");
        printf("Activating PDP context...\n");
        break;

    /* ----------------------------------------------------------------------
     * OPEN TCP SOCKET
     * ----------------------------------------------------------------------
     */

    case LTE_SM_OPEN_SOCKET:
        if (s_current_cmd == LTE_CMD_NONE) {
            printf("Opening TCP socket...\n");
            s_current_cmd = LTE_CMD_TCP_OPEN;
            lte_tcp_open();
            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * MQTT CONNECT
     * ----------------------------------------------------------------------
     */

    case LTE_SM_MQTT_CONNECT:
        if (s_current_cmd == LTE_CMD_NONE) {
            s_current_cmd = LTE_CMD_MQTT_CONNECT;
            printf("Connecting to MQTT broker...\n");
            lte_mqtt_connect("broker.hivemq.com", 1883);
            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * MQTT SUBSCRIBE
     * ----------------------------------------------------------------------
     */

    case LTE_SM_MQTT_SUBSCRIBE:
        if (s_current_cmd == LTE_CMD_NONE) {
            s_current_cmd = LTE_CMD_MQTT_SUBSCRIBE;
            printf("Subscribing to MQTT topic...\n");
            lte_mqtt_subscribe("device/commands");
            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * RUN STATE
     * ----------------------------------------------------------------------
     *
     * Production:
     *   - Normal operation
     *   - TCP/MQTT traffic
     *
     * Test mode:
     *   - No forward path
     *   - No runtime behavior
     */

    case LTE_SM_RUN:
        break;

    /* ----------------------------------------------------------------------
     * Unexpected state
     * ----------------------------------------------------------------------
     */

    default:
        printf("Unexpected state encountered\n");
        lte_sm_set_state(LTE_SM_ERROR);
        break;
    }
}

/******************************************************************************
 * END OF SECTION 4
 ******************************************************************************/
/******************************************************************************
 * SECTION 5 — TIMEOUT WATCHDOG
 *
 * The timeout watchdog ensures that the modem does not “hang” forever waiting
 * for a response to an AT command.
 *
 * Every command has a timeout:
 *   - Attach:         15 seconds
 *   - PDP:            10 seconds
 *   - TCP open:        8 seconds
 *   - MQTT connect:    8 seconds
 *   - MQTT subscribe:  8 seconds
 *
 * If the modem does not respond within the timeout window:
 *      → The state machine enters LTE_SM_ERROR
 *      → Recovery logic decides what to do next
 *
 * In TEST MODE:
 *   - Timeouts almost never trigger because tests inject responses instantly.
 *   - Forward path is disabled, so no commands are sent automatically.
 *   - Timeout watchdog still runs, but s_current_cmd is usually NONE.
 ******************************************************************************/

/* ============================================================================
 * 17. check_timeout()
 * ============================================================================
 *
 * This function is called on EVERY event (including forward-path ticks).
 *
 * Behavior:
 *   1. If no command is active (s_current_cmd == NONE), do nothing.
 *   2. Look up the timeout for the active command.
 *   3. Compare current time with the timestamp when the command was sent.
 *   4. If the timeout has expired:
 *        - Print a debug message
 *        - Transition to LTE_SM_ERROR
 *
 * NOTE FOR TEST MODE:
 *   - Because forward path is disabled, s_current_cmd is usually NONE.
 *   - Therefore, timeouts do NOT interfere with your test scenarios.
 */

static void check_timeout(void)
{
    /* No active command → nothing to time out */
    if (s_current_cmd == LTE_CMD_NONE)
        return;

    /* Get timeout for the active command */
    uint32_t timeout = get_cmd_timeout(s_current_cmd);

    /* Commands like NONE return timeout = 0 */
    if (timeout == 0)
        return;

    /* Check if the command exceeded its timeout window */
    if (lte_hw_ms() - s_state_ts > timeout) {
        printf("TIMEOUT on command %d\n", s_current_cmd);
        /* Enter ERROR state — recovery logic will handle it */
        lte_sm_set_state(LTE_SM_ERROR);
    }
}

/******************************************************************************
 * END OF SECTION 5
 ******************************************************************************/
/******************************************************************************
 * SECTION 6 — ERROR → RECOVERY MAPPING
 *
 * When the state machine enters LTE_SM_ERROR, it must decide what recovery
 * action to take. This decision depends entirely on the *previous* state.
 *
 * Example:
 *   - If attach failed → retry attach
 *   - If PDP failed → retry PDP
 *   - If TCP failed → reopen socket
 *   - If MQTT connect failed → reconnect
 *   - If MQTT subscribe failed → resubscribe
 *
 * This mapping ensures the modem can self-heal from transient failures.
 *
 * TEST MODE:
 *   Your test harness relies heavily on this mapping.
 *   Every scenario that injects "ERROR" expects the correct recovery state:
 *
 *      LTE_SM_ATTACH         → LTE_SM_REATTACH
 *      LTE_SM_PDP            → LTE_SM_REPDP
 *      LTE_SM_OPEN_SOCKET    → LTE_SM_REOPEN_SOCKET
 *      LTE_SM_MQTT_CONNECT   → LTE_SM_MQTT_RECONNECT
 *      LTE_SM_MQTT_SUBSCRIBE → LTE_SM_MQTT_RESUBSCRIBE
 *
 * If this mapping is wrong, ALL your tests will fail.
 ******************************************************************************/

static void map_error_to_recovery(void)
{   

    printf("ERROR in state %d (prev=%d)\n", lte_sm_get_state(), s_prev_state);

    switch (s_prev_state) {

    /* ----------------------------------------------------------------------
     * ATTACH ERROR → REATTACH
     * ----------------------------------------------------------------------
     *
     * If attach fails, retry attach.
     * This matches your test scenario:
     *   scenario_attach_fail_then_success()
     */

    case LTE_SM_ATTACH:
        lte_sm_set_state(LTE_SM_REATTACH);
        break;

    /* ----------------------------------------------------------------------
     * PDP ERROR → REPDP
     * ----------------------------------------------------------------------
     *
     * If PDP activation fails, retry PDP.
     * Matches:
     *   scenario_pdp_fail_then_success()
     */

    case LTE_SM_PDP:
        lte_sm_set_state(LTE_SM_REPDP);
        break;

    /* ----------------------------------------------------------------------
     * TCP ERROR → REOPEN_SOCKET
     * ----------------------------------------------------------------------
     *
     * If TCP open fails, retry opening the socket.
     * Matches:
     *   scenario_tcp_fail_then_success()
     */

    case LTE_SM_OPEN_SOCKET:
        lte_sm_set_state(LTE_SM_REOPEN_SOCKET);
        break;

    /* ----------------------------------------------------------------------
     * MQTT CONNECT ERROR → MQTT_RECONNECT
     * ----------------------------------------------------------------------
     *
     * If MQTT connect fails, retry connecting.
     * Matches:
     *   scenario_mqtt_connect_fail_then_success()
     */

    case LTE_SM_MQTT_CONNECT:
        lte_sm_set_state(LTE_SM_MQTT_RECONNECT);
        break;

    /* ----------------------------------------------------------------------
     * MQTT SUBSCRIBE ERROR → MQTT_RESUBSCRIBE
     * ----------------------------------------------------------------------
     *
     * If subscription fails, retry subscription.
     * Matches:
     *   scenario_mqtt_sub_fail_then_success()
     */

    case LTE_SM_MQTT_SUBSCRIBE:
        lte_sm_set_state(LTE_SM_MQTT_RESUBSCRIBE);
        break;

    /* ----------------------------------------------------------------------
     * Unexpected error → REBOOT MODEM
     * ----------------------------------------------------------------------
     *
     * If error occurs in any other state, reboot the modem.
     * This is a safe fallback for production.
     */

    default:
        lte_sm_set_state(LTE_SM_REBOOT_MODEM);
        printf("Unexpected error mapping from state %d, rebooting modem\n",
               s_prev_state);
        break;
    }
}

/******************************************************************************
 * END OF SECTION 6
 ******************************************************************************/
/******************************************************************************
 * SECTION 7 — +CEREG URC HANDLER
 *
 * The modem sends unsolicited registration status messages (URCs) such as:
 *
 *     +CEREG: <n>,<stat>
 *
 * Where <stat> indicates network registration status:
 *
 *     0 → Not registered, not searching
 *     1 → Registered (home network)
 *     2 → Searching
 *     3 → Registration denied
 *     5 → Registered (roaming)
 *
 * PRODUCTION BEHAVIOR:
 *   - If network is lost (stat = 0), enter ERROR → recovery.
 *   - If registration denied (stat = 3), enter ERROR → recovery.
 *   - If registered (stat = 1 or 5), do nothing.
 *
 * TEST MODE:
 *   - Your test harness does NOT inject +CEREG URCs.
 *   - Therefore this handler does NOT interfere with tests.
 *   - It must remain enabled for production stability.
 ******************************************************************************/

static void lte_handle_cereg_urc(const char *line)
{
    int stat = -1;

    /* Parse the URC: +CEREG: <n>,<stat> */
    if (sscanf(line, "+CEREG: %*d,%d", &stat) == 1) {

        printf("Received +CEREG URC: status=%d\n", stat);

        switch (stat) {

        /* --------------------------------------------------------------
         * 0 → Network lost
         * --------------------------------------------------------------
         *
         * Production:
         *   - Enter ERROR
         *   - Recovery logic will reattach
         *
         * Test mode:
         *   - Not used
         */

        case 0:
            printf("URC: Network lost\n");
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
            break;

        /* --------------------------------------------------------------
         * 1 or 5 → Registered (home or roaming)
         * --------------------------------------------------------------
         *
         * Production:
         *   - Normal condition
         *
         * Test mode:
         *   - Ignored
         */

        case 1:
        case 5:
            printf("URC: Network registered\n");
            break;

        /* --------------------------------------------------------------
         * 2 → Searching
         * --------------------------------------------------------------
         *
         * Production:
         *   - Informational only
         *
         * Test mode:
         *   - Ignored
         */

        case 2:
            printf("URC: Searching...\n");
            break;

        /* --------------------------------------------------------------
         * 3 → Registration denied
         * --------------------------------------------------------------
         *
         * Production:
         *   - Enter ERROR
         *   - Recovery logic will reattach
         *
         * Test mode:
         *   - Not used
         */

        case 3:
            printf("URC: Registration denied\n");
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
            break;

        /* --------------------------------------------------------------
         * Unknown status → treat as error
         * --------------------------------------------------------------
         */

        default:
            printf("URC: Unknown CEREG status %d\n", stat);
            s_current_cmd = LTE_CMD_NONE;
            lte_sm_set_state(LTE_SM_ERROR);
            break;
        }
    }
}

/******************************************************************************
 * END OF SECTION 7
 ******************************************************************************/
/******************************************************************************
 * SECTION 8 — RECOVERY STATE HANDLERS
 *
 * When the state machine enters a recovery state (LTE_SM_REATTACH, LTE_SM_REPDP,
 * LTE_SM_REOPEN_SOCKET, LTE_SM_MQTT_RECONNECT, LTE_SM_MQTT_RESUBSCRIBE),
 * it must resend the appropriate command and transition back to the normal
 * forward-path state.
 *
 * These handlers are triggered ONLY when:
 *   - A command returns "ERROR", OR
 *   - A timeout occurs, OR
 *   - A URC indicates network loss
 *
 * TEST MODE:
 *   Your test harness relies heavily on these recovery handlers.
 *
 * Example:
 *   Scenario: PDP fail → success
 *     Inject "ERROR" during PDP
 *     → State becomes LTE_SM_REPDP
 *     Recovery handler sends AT+CGACT again
 *     → State returns to LTE_SM_PDP
 *
 * If ANY of these handlers are wrong, your tests WILL fail.
 ******************************************************************************/

static void handle_recovery(void)
{
  /* Decide what recovery action to take based on the current state */

    switch (s_state) {

    /* ----------------------------------------------------------------------
     * LTE_SM_REBOOT_MODEM
     * ----------------------------------------------------------------------
     *
     * This is the fallback recovery state for unexpected errors.
     * Production behavior:
     *   - Reset modem hardware
     *   - Return to WAIT_BOOT
     *
     * Test mode:
     *   - Rarely used
     */

    case LTE_SM_REBOOT_MODEM:
        lte_hw_reset_modem();
        lte_sm_set_state(LTE_SM_WAIT_BOOT);
        break;

    /* ----------------------------------------------------------------------
     * LTE_SM_REATTACH
     * ----------------------------------------------------------------------
     *
     * Recovery from attach failure.
     *
     * Production:
     *   - Resend AT+CGATT=1
     *   - Return to LTE_SM_ATTACH
     *
     * Test mode:
     *   - Your test harness expects EXACTLY this behavior.
     *   - Scenario: attach_fail_then_success()
     */

    case LTE_SM_REATTACH:
        send_cmd_once(LTE_CMD_ATTACH, "AT+CGATT=1\r\n");
        lte_sm_set_state(LTE_SM_ATTACH);
        break;

    /* ----------------------------------------------------------------------
     * LTE_SM_REPDP
     * ----------------------------------------------------------------------
     *
     * Recovery from PDP activation failure.
     *
     * Production:
     *   - Resend AT+CGACT=1,1
     *   - Return to LTE_SM_PDP
     *
     * Test mode:
     *   - Scenario: pdp_fail_then_success()
     */

    case LTE_SM_REPDP:
        send_cmd_once(LTE_CMD_PDP, "AT+CGACT=1,1\r\n");
        lte_sm_set_state(LTE_SM_PDP);
        break;

    /* ----------------------------------------------------------------------
     * LTE_SM_REOPEN_SOCKET
     * ----------------------------------------------------------------------
     *
     * Recovery from TCP open failure.
     *
     * Production:
     *   - Call lte_tcp_open()
     *   - Return to LTE_SM_OPEN_SOCKET
     *
     * Test mode:
     *   - Scenario: tcp_fail_then_success()
     */

    case LTE_SM_REOPEN_SOCKET:
        s_current_cmd = LTE_CMD_TCP_OPEN;
        lte_tcp_open();
        lte_sm_set_state(LTE_SM_OPEN_SOCKET);
        break;

    /* ----------------------------------------------------------------------
     * LTE_SM_MQTT_RECONNECT
     * ----------------------------------------------------------------------
     *
     * Recovery from MQTT connect failure.
     *
     * Production:
     *   - Call lte_mqtt_connect()
     *   - Return to LTE_SM_MQTT_CONNECT
     *
     * Test mode:
     *   - Scenario: mqtt_connect_fail_then_success()
     */

    case LTE_SM_MQTT_RECONNECT:
        s_current_cmd = LTE_CMD_MQTT_CONNECT;
        lte_mqtt_connect("broker.hivemq.com", 1883);
        lte_sm_set_state(LTE_SM_MQTT_CONNECT);
        break;

    /* ----------------------------------------------------------------------
     * LTE_SM_MQTT_RESUBSCRIBE
     * ----------------------------------------------------------------------
     *
     * Recovery from MQTT subscribe failure.
     *
     * Production:
     *   - Call lte_mqtt_subscribe()
     *   - Return to LTE_SM_MQTT_SUBSCRIBE
     *
     * Test mode:
     *   - Scenario: mqtt_sub_fail_then_success()
     */

    case LTE_SM_MQTT_RESUBSCRIBE:
        s_current_cmd = LTE_CMD_MQTT_SUBSCRIBE;
        lte_mqtt_subscribe("device/commands");
        lte_sm_set_state(LTE_SM_MQTT_SUBSCRIBE);
        break;

    /* ----------------------------------------------------------------------
     * Unexpected recovery state
     * ----------------------------------------------------------------------
     */

    default:
        lte_sm_set_state(LTE_SM_ERROR);
        break;
    }
}

/******************************************************************************
 * END OF SECTION 8
 ******************************************************************************/
/******************************************************************************
 * SECTION 9 — FORCE STATE API (TESTING ONLY)
 *
 * lte_sm_force_state()
 *
 * This function allows the test harness to override the LTE state machine
 * and place it directly into a specific state.
 *
 * IMPORTANT:
 *   - This function does NOT update s_prev_state.
 *   - This function does NOT update timestamps.
 *   - This function does NOT print debug output.
 *   - This function does NOT trigger forward-path behavior.
 *
 * It is intentionally minimal because it is used ONLY for testing.
 *
 * Example usage in your test harness:
 *
 *     reset_lte();
 *     lte_sm_force_state(LTE_SM_ATTACH);
 *
 * This ensures tests begin at the ATTACH state without running the boot
 * sequence or forward path.
 ******************************************************************************/

void lte_sm_force_state(LteSmState_t st)
{
    /* Directly override the current state */
    s_state = st;

    /* NOTE:
     * We intentionally do NOT update:
     *   - s_prev_state
     *   - s_state_ts
     *   - s_current_cmd
     *
     * This keeps the function safe for test-mode use.
     */
}

/******************************************************************************
 * SECTION 10 — PUBLIC API
 *
 * These functions are the external interface of the LTE state machine.
 * They are called by:
 *   - Application code
 *   - Test harness
 *   - Driver callbacks
 *
 * This section includes:
 *   1. lte_sm_get_state()     → returns current LTE state
 *   2. lte_sm_forward_path()  → runs forward-path logic (production only)
 ******************************************************************************/

/* ============================================================================
 * 18. lte_sm_get_state()
 * ============================================================================
 *
 * Returns the current LTE state.
 *
 * This function is used by:
 *   - Application logic (to check if modem is ready)
 *   - Test harness (assert_state(expected))
 *
 * NOTE:
 *   This function does NOT modify any internal state.
 *   It is safe to call at any time.
 */

LteSmState_t lte_sm_get_state(void)
{
    return s_state;
}

/* ============================================================================
 * 19. lte_sm_forward_path()
 * ============================================================================
 *
 * This function runs the forward path (production command dispatch).
 *
 * In PRODUCTION:
 *   - Called periodically (e.g., every 10–100 ms)
 *   - Decides which AT command to send next
 *   - Drives the modem through boot → attach → PDP → TCP → MQTT → RUN
 *
 * In TEST MODE:
 *   sm_forward_enabled = false
 *   → forward path is completely disabled
 *   → NO commands are sent automatically
 *   → ONLY injected test lines drive the state machine
 *
 * This is EXACTLY what your test harness requires.
 */

void lte_sm_forward_path(void)
{
    /* Run production forward-path logic */
    handle_line_forward(NULL);
}

/******************************************************************************
 * SECTION 9B — TEST-MODE CONTROL API
 *
 * These functions allow the test harness to disable parts of the production
 * logic so tests can run deterministically.
 *
 * They simply toggle the internal flags:
 *   sm_forward_enabled
 *   sm_boot_enabled
 *   sm_attach_enabled
 *
 * IMPORTANT:
 *   - DO NOT disable attach handler in tests unless you want tests to freeze.
 *   - Forward path MUST be disabled in tests.
 *   - Boot sequence MUST be disabled in tests.
 ******************************************************************************/

void lte_sm_disable_forward_path(void)
{
    sm_forward_enabled = false;
}

void lte_sm_disable_boot_sequence(void)
{
    sm_boot_enabled = false;
}

void lte_sm_disable_attach_handler(void)
{
    sm_attach_enabled = false;
}

/* Optional: enable functions */

void lte_sm_enable_forward_path(void)
{
    sm_forward_enabled = true;
}

void lte_sm_enable_boot_sequence(void)
{
    sm_boot_enabled = true;
}

void lte_sm_enable_attach_handler(void)
{
    sm_attach_enabled = true;
}

/******************************************************************************
 * END OF SECTION 10
 ******************************************************************************/

/******************************************************************************
 * SECTION 11 — EVENT HANDLER (MAIN ENTRY POINT)
 *
 * This is the central dispatcher for ALL LTE state machine activity.
 *
 * Every event enters the state machine through this function:
 *
 *   - RX line from modem
 *   - Timeout event
 *   - Error event
 *   - Forward-path tick (NULL event)
 *
 * The event handler performs the following steps:
 *
 *   1. Check for command timeout
 *   2. If event is NULL → run forward path (production only)
 *   3. If event is RX line:
 *        - Handle +CEREG URCs
 *        - Route line to the correct command handler
 *        - Handle ERROR state transitions
 *        - Handle recovery states
 *        - Run forward path if needed
 *   4. If event is TIMEOUT or ERROR → enter LTE_SM_ERROR
 *
 * TEST MODE:
 *   - Forward path is disabled
 *   - Boot sequence is disabled
 *   - Attach handler MUST remain enabled
 *   - Tests rely entirely on injected RX lines
 ******************************************************************************/

void lte_sm_handle_event(const LteEvent_t *ev)
{    

    
    /* ----------------------------------------------------------------------
     * STEP 1 — TIMEOUT CHECK
     * ----------------------------------------------------------------------
     *
     * Always check for command timeout first.
     * If a timeout occurs, the state machine enters LTE_SM_ERROR.
     */

    check_timeout();

    /* ----------------------------------------------------------------------
     * STEP 2 — NULL EVENT (FORWARD PATH TICK)
     * ----------------------------------------------------------------------
     *
     * If ev == NULL:
     *   - In production: run forward path
     *   - In test mode: forward path disabled → do nothing
     */

    if (!ev) {       
        lte_sm_forward_path();
        return;
    }

    /* ----------------------------------------------------------------------
     * STEP 3 — RX LINE EVENT
     * ----------------------------------------------------------------------
     *
     * This is the most common event type.
     * The modem has sent a line of text.
     */

    if (ev->type == LTE_EVENT_RX_LINE) {

        printf("LTE state machine received line: %s\n", ev->line);
        printf("Current command: %d\n", s_current_cmd);
        printf("Current state: %d\n", s_state);
        printf("Previous state: %d\n", s_prev_state);  

        /* --------------------------------------------------------------
         * 3A — Handle +CEREG URCs first
         * --------------------------------------------------------------
         *
         * URCs are unsolicited and must be processed before command
         * handlers, because they may indicate network loss.
         */

        if (strncmp(ev->line, "+CEREG:", 7) == 0) {
            lte_handle_cereg_urc(ev->line);
            return;
        }

        /* --------------------------------------------------------------
         * 3B — Route line to the correct command handler
         * --------------------------------------------------------------
         *
         * The active command (s_current_cmd) determines which handler
         * should process the incoming line.
         */

        switch (s_current_cmd) { 

        case LTE_CMD_ATTACH:
            lte_cmd_attach_handle(ev->line);
            printf("LTE state machine ATTACH handler processed line: %s\n", ev->line);
            return;

        case LTE_CMD_PDP:
            lte_cmd_pdp_handle(ev->line);
            printf("LTE state machine PDP handler processed line: %s\n", ev->line);
            return;

        case LTE_CMD_TCP_OPEN:
            lte_cmd_tcp_handle(ev->line);
            printf("LTE state machine TCP handler processed line: %s\n", ev->line);
            return;

        case LTE_CMD_MQTT_CONNECT:
            lte_cmd_mqtt_conn_handle(ev->line);
            printf("LTE state machine MQTT CONNECT handler processed line: %s\n", ev->line);
            return;

        case LTE_CMD_MQTT_SUBSCRIBE:
            lte_cmd_mqtt_sub_handle(ev->line);
            printf("LTE state machine MQTT SUBSCRIBE handler processed line: %s\n", ev->line);
            return;

        case LTE_CMD_NONE:
            break;  /* No active command → fall through */
        }

        /* --------------------------------------------------------------
         * 3C — If state machine is in ERROR, map to recovery state
         * --------------------------------------------------------------
         */

        if (s_state == LTE_SM_ERROR) {
            printf("Mapping ERROR state to recovery state based on previous state %d\n", s_prev_state); 
            map_error_to_recovery();
            return;
        }

        /* --------------------------------------------------------------
         * 3D — If in recovery state, run recovery handler
         * --------------------------------------------------------------
         */

        if (s_state >= LTE_SM_REBOOT_MODEM &&
            s_state <= LTE_SM_MQTT_RESUBSCRIBE) {
            printf("Running recovery handler for state %d\n", s_state);
            handle_recovery();
            return;
        }

        /* --------------------------------------------------------------
         * 3E — Otherwise, run forward path logic
         * --------------------------------------------------------------
         *
         * In production:
         *   - This drives the modem through boot → attach → PDP → etc.
         *
         * In test mode:
         *   - Forward path is disabled
         *   - Tests rely ONLY on injected lines
         */

        handle_line_forward(ev->line);
        return;
    }

    /* ----------------------------------------------------------------------
     * STEP 4 — TIMEOUT or ERROR EVENT
     * ----------------------------------------------------------------------
     *
     * These events force the state machine into LTE_SM_ERROR.
     * Recovery logic will handle the next step.
     */

    if (ev->type == LTE_EVENT_TIMEOUT ||
        ev->type == LTE_EVENT_ERROR) {
        lte_sm_set_state(LTE_SM_ERROR);
        return;
    }
}

/******************************************************************************
 * END OF SECTION 11 — END OF FULL LTE STATE MACHINE
 ******************************************************************************/

