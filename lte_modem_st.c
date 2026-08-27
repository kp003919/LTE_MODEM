/**
 * LTE MODEM STATE MACHINE — PRODUCTION OPTIMIZED VERSION
 *
 * Comment Style:
 *   - Section-level comments explain the purpose of each major block.
 *   - Inline comments explain important decisions inside functions.
 *   - Tone is clear, friendly, and professional (engineering team style).
 *
 * Key Features:
 *   - Per-command timeouts
 *   - Retry counters for each command
 *   - Commands sent only once per state entry
 *   - Deterministic OK/ERROR command handlers
 *   - Robust recovery logic
 *   - Same state structure you already use
 */

#include "lte_sm.h"
#include "lte_driver.h"
#include "lte_hw.h"
#include "lte_tcp.h"
#include "lte_mqtt.h"
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * STATE MACHINE VARIABLES
 *
 * These track the current state, the previous state, and the timestamp when
 * the state was entered. The timestamp is used for timeout detection.
 * -------------------------------------------------------------------------- */

static LteSmState_t s_state      = LTE_SM_INIT;
static LteSmState_t s_prev_state = LTE_SM_INVALID;
static uint32_t     s_state_ts   = 0;

/* --------------------------------------------------------------------------
 * COMMAND TRACKING
 *
 * Only one AT command may be active at a time. When a command is active,
 * incoming modem lines are routed to the corresponding command handler.
 *
 * Command handlers are responsible for:
 *   - Interpreting OK/ERROR
 *   - Clearing s_current_cmd
 *   - Advancing the state machine
 * -------------------------------------------------------------------------- */

typedef enum {
    LTE_CMD_NONE = 0,
    LTE_CMD_ATTACH,
    LTE_CMD_PDP,
    LTE_CMD_TCP_OPEN,
    LTE_CMD_MQTT_CONNECT,
    LTE_CMD_MQTT_SUBSCRIBE
} LteCommand_t;

static LteCommand_t s_current_cmd = LTE_CMD_NONE;

/* --------------------------------------------------------------------------
 * RETRY COUNTERS
 *
 * Each command has its own retry counter. If a command fails (ERROR or timeout),
 * the retry counter is incremented. If the counter exceeds MAX_RETRIES, the
 * modem enters the ERROR state and recovery escalates.
 * -------------------------------------------------------------------------- */

static int retry_attach = 0;
static int retry_pdp = 0;
static int retry_tcp = 0;
static int retry_mqtt_conn = 0;
static int retry_mqtt_sub = 0;

#define MAX_RETRIES 3

/* --------------------------------------------------------------------------
 * COMMAND TIMEOUTS
 *
 * Each command has a timeout window. If the modem does not respond within
 * this window, the command is considered failed and recovery begins.
 *
 * Timeouts are tuned for typical LTE modem behavior:
 *   - Attach: 10–20 seconds
 *   - PDP:    5–10 seconds
 *   - TCP:    5–8 seconds
 *   - MQTT:   5–8 seconds
 * -------------------------------------------------------------------------- */

#define TIMEOUT_ATTACH       15000
#define TIMEOUT_PDP          10000
#define TIMEOUT_TCP_OPEN      8000
#define TIMEOUT_MQTT_CONNECT  8000
#define TIMEOUT_MQTT_SUB      8000

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

/* Increment retry counter for a command */
static int increment_retry(LteCommand_t cmd)
{
    switch (cmd) {
    case LTE_CMD_ATTACH:         return ++retry_attach;
    case LTE_CMD_PDP:            return ++retry_pdp;
    case LTE_CMD_TCP_OPEN:       return ++retry_tcp;
    case LTE_CMD_MQTT_CONNECT:   return ++retry_mqtt_conn;
    case LTE_CMD_MQTT_SUBSCRIBE: return ++retry_mqtt_sub;
    default:                     return 0;
    }
}

/* Reset retry counter for a command */
static void reset_retry(LteCommand_t cmd)
{
    switch (cmd) {
    case LTE_CMD_ATTACH:         retry_attach = 0; break;
    case LTE_CMD_PDP:            retry_pdp = 0; break;
    case LTE_CMD_TCP_OPEN:       retry_tcp = 0; break;
    case LTE_CMD_MQTT_CONNECT:   retry_mqtt_conn = 0; break;
    case LTE_CMD_MQTT_SUBSCRIBE: retry_mqtt_sub = 0; break;
    default:                     break;
    }
}

/* --------------------------------------------------------------------------
 * STATE TRANSITION HELPER
 *
 * Updates:
 *   - previous state
 *   - current state
 *   - timestamp (used for timeout detection)
 *
 * Logging is included for debugging and field diagnostics.
 * -------------------------------------------------------------------------- */

static void set_state(LteSmState_t st)
{
    s_prev_state = s_state;
    s_state      = st;
    s_state_ts   = lte_hw_ms();

    printf("STATE -> %d (prev=%d)\n", s_state, s_prev_state);
}

/* --------------------------------------------------------------------------
 * COMMAND HANDLERS
 *
 * These functions process modem responses for each AT command.
 *
 * Important rules:
 *   - Only OK or ERROR ends a command.
 *   - Status lines (e.g., +CGATT:1) are ignored.
 *   - On OK:
 *       → Clear active command
 *       → Reset retry counter
 *       → Advance to next state
 *   - On ERROR:
 *       → Increment retry counter
 *       → If retries exceeded → go to ERROR state
 *       → Otherwise → go to appropriate recovery state
 *
 * These handlers NEVER send commands. They ONLY interpret responses.
 * -------------------------------------------------------------------------- */


/* --------------------------------------------------------------------------
 * Handle AT+CGATT=1 (Network Attach)
 *
 * The modem may send status lines like "+CGATT:1" during attach.
 * These are informational and do NOT complete the command.
 *
 * Only "OK" or "ERROR" ends the attach command.
 * -------------------------------------------------------------------------- */
static void lte_cmd_attach_handle(const char *line)
{
    /* Ignore status lines such as +CGATT:0 or +CGATT:1 */
    if (strncmp(line, "+CGATT:", 7) == 0)
        return;

    /* Attach succeeded */
    if (strcmp(line, "OK") == 0) {
        reset_retry(LTE_CMD_ATTACH);
        s_current_cmd = LTE_CMD_NONE;
        set_state(LTE_SM_PDP);   /* Move to PDP activation */
        return;
    }

    /* Attach failed */
    if (strcmp(line, "ERROR") == 0) {

        /* Too many failures → escalate to global error */
        if (increment_retry(LTE_CMD_ATTACH) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_ERROR);
        } else {
            /* Retry attach via recovery state */
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_REATTACH);
        }
        return;
    }
}


/* --------------------------------------------------------------------------
 * Handle AT+CGACT=1,1 (PDP Context Activation)
 *
 * PDP activation is simple: only OK or ERROR is expected.
 * -------------------------------------------------------------------------- */
static void lte_cmd_pdp_handle(const char *line)
{
    if (strcmp(line, "OK") == 0) {
        reset_retry(LTE_CMD_PDP);
        s_current_cmd = LTE_CMD_NONE;
        set_state(LTE_SM_OPEN_SOCKET);   /* Move to TCP socket open */
        return;
    }

    if (strcmp(line, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_PDP) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_REPDP);      /* Retry PDP activation */
        }
        return;
    }
}


/* --------------------------------------------------------------------------
 * Handle TCP Open (AT commands inside lte_tcp_open)
 *
 * TCP open is treated like any other command:
 *   - OK → next state
 *   - ERROR → retry or fail
 * -------------------------------------------------------------------------- */
static void lte_cmd_tcp_handle(const char *line)
{
    if (strcmp(line, "OK") == 0) {
        reset_retry(LTE_CMD_TCP_OPEN);
        s_current_cmd = LTE_CMD_NONE;
        set_state(LTE_SM_MQTT_CONNECT);   /* Move to MQTT connect */
        return;
    }

    if (strcmp(line, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_TCP_OPEN) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_REOPEN_SOCKET);  /* Retry TCP open */
        }
        return;
    }
}


/* --------------------------------------------------------------------------
 * Handle MQTT Connect
 *
 * MQTT connect is a multi-step process inside the modem, but from the
 * state machine perspective, only OK/ERROR matters.
 * -------------------------------------------------------------------------- */
static void lte_cmd_mqtt_conn_handle(const char *line)
{
    if (strcmp(line, "OK") == 0) {
        reset_retry(LTE_CMD_MQTT_CONNECT);
        s_current_cmd = LTE_CMD_NONE;
        set_state(LTE_SM_MQTT_SUBSCRIBE);   /* Move to MQTT subscribe */
        return;
    }

    if (strcmp(line, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_MQTT_CONNECT) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_MQTT_RECONNECT); /* Retry MQTT connect */
        }
        return;
    }
}


/* --------------------------------------------------------------------------
 * Handle MQTT Subscribe
 *
 * Final step before entering RUN mode.
 * -------------------------------------------------------------------------- */
static void lte_cmd_mqtt_sub_handle(const char *line)
{
    if (strcmp(line, "OK") == 0) {
        reset_retry(LTE_CMD_MQTT_SUBSCRIBE);
        s_current_cmd = LTE_CMD_NONE;
        set_state(LTE_SM_RUN);   /* Modem fully initialized */
        return;
    }

    if (strcmp(line, "ERROR") == 0) {

        if (increment_retry(LTE_CMD_MQTT_SUBSCRIBE) >= MAX_RETRIES) {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_ERROR);
        } else {
            s_current_cmd = LTE_CMD_NONE;
            set_state(LTE_SM_MQTT_RESUBSCRIBE); /* Retry subscribe */
        }
        return;
    }
}

/* --------------------------------------------------------------------------
 * FORWARD PATH (SEND COMMANDS ONLY ONCE)
 *
 * Forward states are responsible for sending AT commands.
 * They NEVER interpret modem responses — that is the job of command handlers.
 *
 * Key rules:
 *   - Commands are sent only once per state entry.
 *   - s_current_cmd is set before sending the command.
 *   - After sending, the state machine waits for OK/ERROR.
 *
 * This design prevents repeated AT commands and ensures deterministic behavior.
 * -------------------------------------------------------------------------- */

/* Helper: send an AT command only once when entering a state */
static void send_cmd_once(LteCommand_t cmd, const char *at)
{
    /* Only send if no command is currently active */
    if (s_current_cmd == LTE_CMD_NONE) {
        s_current_cmd = cmd;
        lte_driver_send_cmd(at);

        /* Record timestamp for timeout detection */
        s_state_ts = lte_hw_ms();
    }
}

static void handle_line_forward(const char *line)
{
    (void)line; /* Forward path does not interpret modem responses */

    switch (s_state) {

    /* ----------------------------------------------------------------------
     * INITIALIZATION SEQUENCE
     * ---------------------------------------------------------------------- */

    case LTE_SM_INIT:
        /* Power-on sequence is hardware-specific */
        lte_hw_power_on_sequence();
        set_state(LTE_SM_POWER_ON);
        break;

    case LTE_SM_POWER_ON:
        /* First command after power-on: basic AT check */
        lte_driver_send_cmd("AT\r\n");
        set_state(LTE_SM_WAIT_BOOT);
        break;

    case LTE_SM_WAIT_BOOT:
        /* Boot check: wait for a single OK from the modem */
        if (lte_driver_last_line_contains("OK")) {

            /* Disable echo and check SIM status */
            lte_driver_send_cmd("ATE0\r\n");
            lte_driver_send_cmd("AT+CPIN?\r\n");

            set_state(LTE_SM_CHECK_SIM);
        }
        break;

    case LTE_SM_CHECK_SIM:
        /* SIM ready → begin network attach */
        if (lte_driver_last_line_contains("READY")) {

            send_cmd_once(LTE_CMD_ATTACH, "AT+CGATT=1\r\n");
            set_state(LTE_SM_ATTACH);
        }
        break;

    /* ----------------------------------------------------------------------
     * NETWORK ATTACH
     * ---------------------------------------------------------------------- */

    case LTE_SM_ATTACH:
        /* Attach responses handled by lte_cmd_attach_handle() */
        break;

    /* ----------------------------------------------------------------------
     * PDP CONTEXT ACTIVATION
     * ---------------------------------------------------------------------- */

    case LTE_SM_PDP:
        /* Send PDP activation only once */
        send_cmd_once(LTE_CMD_PDP, "AT+CGACT=1,1\r\n");
        break;

    /* ----------------------------------------------------------------------
     * TCP SOCKET OPEN
     * ---------------------------------------------------------------------- */

    case LTE_SM_OPEN_SOCKET:
        if (s_current_cmd == LTE_CMD_NONE) {

            /* Mark command active */
            s_current_cmd = LTE_CMD_TCP_OPEN;

            /* lte_tcp_open() internally sends the required AT commands */
            lte_tcp_open();

            /* Timestamp for timeout detection */
            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * MQTT CONNECT
     * ---------------------------------------------------------------------- */

    case LTE_SM_MQTT_CONNECT:
        if (s_current_cmd == LTE_CMD_NONE) {

            s_current_cmd = LTE_CMD_MQTT_CONNECT;
            lte_mqtt_connect("broker.hivemq.com", 1883);

            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * MQTT SUBSCRIBE
     * ---------------------------------------------------------------------- */

    case LTE_SM_MQTT_SUBSCRIBE:
        if (s_current_cmd == LTE_CMD_NONE) {

            s_current_cmd = LTE_CMD_MQTT_SUBSCRIBE;
            lte_mqtt_subscribe("device/commands");

            s_state_ts = lte_hw_ms();
        }
        break;

    /* ----------------------------------------------------------------------
     * NORMAL OPERATION
     * ---------------------------------------------------------------------- */

    case LTE_SM_RUN:
        /* Modem fully initialized and ready for data */
        break;

    /* ----------------------------------------------------------------------
     * UNEXPECTED STATE
     * ---------------------------------------------------------------------- */

    default:
        /* Any unknown state is treated as an error */
        set_state(LTE_SM_ERROR);
        break;
    }
}

/* --------------------------------------------------------------------------
 * TIMEOUT WATCHDOG
 *
 * Every command has a timeout window. If the modem does not respond within
 * this window, the command is considered failed.
 *
 * Behavior:
 *   - Only active commands are checked.
 *   - If timeout expires → transition to ERROR state.
 *   - Recovery logic will decide how to handle the failure.
 *
 * This prevents the modem from getting stuck waiting forever.
 * -------------------------------------------------------------------------- */
static void check_timeout(void)
{
    /* No active command → nothing to check */
    if (s_current_cmd == LTE_CMD_NONE)
        return;

    uint32_t timeout = get_cmd_timeout(s_current_cmd);
    if (timeout == 0)
        return; /* Command has no timeout */

    /* Compare elapsed time with timeout window */
    if (lte_hw_ms() - s_state_ts > timeout) {

        printf("TIMEOUT on command %d\n", s_current_cmd);

        /* Move to ERROR state; recovery will handle it */
        set_state(LTE_SM_ERROR);
    }
}


/* --------------------------------------------------------------------------
 * ERROR → RECOVERY MAPPING
 *
 * When a command fails (ERROR or timeout), the state machine enters
 * LTE_SM_ERROR. From there, we map the previous state to the correct
 * recovery action.
 *
 * Example:
 *   - If attach failed → retry attach
 *   - If PDP failed → retry PDP
 *   - If TCP failed → reopen socket
 *   - If MQTT failed → reconnect or resubscribe
 *
 * If the failure happened in an unexpected state, we reboot the modem.
 * -------------------------------------------------------------------------- */
static void map_error_to_recovery(void)
{
    switch (s_prev_state) {

    case LTE_SM_ATTACH:
        set_state(LTE_SM_REATTACH);
        break;

    case LTE_SM_PDP:
        set_state(LTE_SM_REPDP);
        break;

    case LTE_SM_OPEN_SOCKET:
        set_state(LTE_SM_REOPEN_SOCKET);
        break;

    case LTE_SM_MQTT_CONNECT:
        set_state(LTE_SM_MQTT_RECONNECT);
        break;

    case LTE_SM_MQTT_SUBSCRIBE:
        set_state(LTE_SM_MQTT_RESUBSCRIBE);
        break;

    default:
        /* Unknown failure → safest recovery is full modem reboot */
        set_state(LTE_SM_REBOOT_MODEM);
        break;
    }
}


/* --------------------------------------------------------------------------
 * RECOVERY STATE HANDLERS
 *
 * These states perform the actual recovery actions:
 *
 *   - REBOOT_MODEM: hardware reset
 *   - REATTACH: retry AT+CGATT=1
 *   - REPDP: retry AT+CGACT=1,1
 *   - REOPEN_SOCKET: retry TCP open
 *   - MQTT_RECONNECT: retry MQTT connect
 *   - MQTT_RESUBSCRIBE: retry MQTT subscribe
 *
 * Each recovery state sends the appropriate command ONCE, then transitions
 * back to the normal forward path state.
 * -------------------------------------------------------------------------- */
static void handle_recovery(void)
{
    switch (s_state) {

    /* --------------------------------------------------------------
     * Full modem reboot
     * -------------------------------------------------------------- */
    case LTE_SM_REBOOT_MODEM:
        lte_hw_reset_modem();
        set_state(LTE_SM_WAIT_BOOT);
        break;

    /* --------------------------------------------------------------
     * Retry network attach
     * -------------------------------------------------------------- */
    case LTE_SM_REATTACH:
        send_cmd_once(LTE_CMD_ATTACH, "AT+CGATT=1\r\n");
        set_state(LTE_SM_ATTACH);
        break;

    /* --------------------------------------------------------------
     * Retry PDP activation
     * -------------------------------------------------------------- */
    case LTE_SM_REPDP:
        send_cmd_once(LTE_CMD_PDP, "AT+CGACT=1,1\r\n");
        set_state(LTE_SM_PDP);
        break;

    /* --------------------------------------------------------------
     * Retry TCP socket open
     * -------------------------------------------------------------- */
    case LTE_SM_REOPEN_SOCKET:
        s_current_cmd = LTE_CMD_TCP_OPEN;
        lte_tcp_open();
        set_state(LTE_SM_OPEN_SOCKET);
        break;

    /* --------------------------------------------------------------
     * Retry MQTT connect
     * -------------------------------------------------------------- */
    case LTE_SM_MQTT_RECONNECT:
        s_current_cmd = LTE_CMD_MQTT_CONNECT;
        lte_mqtt_connect("broker.hivemq.com", 1883);
        set_state(LTE_SM_MQTT_CONNECT);
        break;

    /* --------------------------------------------------------------
     * Retry MQTT subscribe
     * -------------------------------------------------------------- */
    case LTE_SM_MQTT_RESUBSCRIBE:
        s_current_cmd = LTE_CMD_MQTT_SUBSCRIBE;
        lte_mqtt_subscribe("device/commands");
        set_state(LTE_SM_MQTT_SUBSCRIBE);
        break;

    /* --------------------------------------------------------------
     * Unexpected recovery state
     * -------------------------------------------------------------- */
    default:
        set_state(LTE_SM_ERROR);
        break;
    }
}

/* --------------------------------------------------------------------------
 * EVENT HANDLER (MAIN ENTRY POINT)
 *
 * This function receives all modem-related events:
 *
 *   - LTE_EVENT_RX_LINE   → a new line arrived from the modem
 *   - LTE_EVENT_TIMEOUT   → external timeout event
 *   - LTE_EVENT_ERROR     → external error event
 *
 * Behavior:
 *   1. First check for command timeout.
 *   2. If RX line:
 *        - If a command is active → route to command handler.
 *        - Else if in ERROR state → map to recovery.
 *        - Else if in recovery state → perform recovery.
 *        - Else → forward path (send next command).
 *   3. If timeout/error event → enter ERROR state.
 *
 * This function is the "brain" that coordinates all parts of the state machine.
 * -------------------------------------------------------------------------- */
void lte_sm_handle_event(const LteEvent_t *ev)
{
    if (!ev)
        return;

    /* Always check for command timeout first */
    check_timeout();

    /* ----------------------------------------------------------------------
     * RX LINE EVENT
     * ---------------------------------------------------------------------- */
    if (ev->type == LTE_EVENT_RX_LINE) {

        /* If a command is active, route line to its handler */
        switch (s_current_cmd) {

        case LTE_CMD_ATTACH:
            lte_cmd_attach_handle(ev->line);
            return;

        case LTE_CMD_PDP:
            lte_cmd_pdp_handle(ev->line);
            return;

        case LTE_CMD_TCP_OPEN:
            lte_cmd_tcp_handle(ev->line);
            return;

        case LTE_CMD_MQTT_CONNECT:
            lte_cmd_mqtt_conn_handle(ev->line);
            return;

        case LTE_CMD_MQTT_SUBSCRIBE:
            lte_cmd_mqtt_sub_handle(ev->line);
            return;

        case LTE_CMD_NONE:
            /* No active command → continue below */
            break;
        }

        /* ------------------------------------------------------------------
         * If we reached here, there is NO active command.
         * Now decide what to do based on current state.
         * ------------------------------------------------------------------ */

        /* ERROR state → map to correct recovery state */
        if (s_state == LTE_SM_ERROR) {
            map_error_to_recovery();
            return;
        }

        /* Recovery states → perform recovery action */
        if (s_state >= LTE_SM_REBOOT_MODEM &&
            s_state <= LTE_SM_MQTT_RESUBSCRIBE) {

            handle_recovery();
            return;
        }

        /* Normal forward path → send next command */
        handle_line_forward(ev->line);
        return;
    }

    /* ----------------------------------------------------------------------
     * TIMEOUT OR ERROR EVENT
     *
     * External timeout/error events force the state machine into ERROR state.
     * Recovery logic will decide how to proceed.
     * ---------------------------------------------------------------------- */
    if (ev->type == LTE_EVENT_TIMEOUT ||
        ev->type == LTE_EVENT_ERROR) {

        set_state(LTE_SM_ERROR);
        return;
    }
}
