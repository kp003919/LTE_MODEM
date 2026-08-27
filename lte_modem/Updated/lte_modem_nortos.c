#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_timer.h"
#include "lte_modem.h"

/** @brief LTE modem states */
// The LTE modem state machine manages the various stages of modem initialization,
// network attachment, and data communication.
// Each state represents a specific step in the process of getting the modem ready
// for normal operation. The state machine transitions between these states based
// on modem responses, timeouts, and error conditions.

typedef enum {
    LTE_STATE_INIT = 0,          // Initial state: set up GPIOs, UART, and timers
    LTE_STATE_POWER_ON,          // Power on the modem by toggling the PWRKEY pin
    LTE_STATE_WAIT_BOOT,         // Wait for the modem to boot and respond to "AT" command
    LTE_STATE_CHECK_SIM,         // Check if the SIM card is ready and available for network registration
    LTE_STATE_ATTACH_NETWORK,    // Attach to the LTE network and check registration status
    LTE_STATE_PDP_ACTIVATE,      // Activate the PDP context for data communication

    // TCP flow:
    // 1. LTE_STATE_OPEN_SOCKET → NETOPEN + TCPCREATE + TCPOPEN
    // 2. LTE_STATE_RUN → send/receive
    // On error:
    // 3. LTE_STATE_REOPEN_SOCKET → recreate socket
    LTE_STATE_OPEN_SOCKET,       // Open a TCP/UDP socket to the specified server for data transmission

    // MQTT flow:
    // 1. LTE_STATE_MQTT_CONNECT → connect to broker
    // 2. LTE_STATE_MQTT_SUBSCRIBE → subscribe to topic
    // 3. LTE_STATE_RUN → publish/receive
    // On error:
    // 4. LTE_STATE_MQTT_RECONNECT → reconnect
    // 5. LTE_STATE_MQTT_RESUBSCRIBE → resubscribe
    LTE_STATE_MQTT_CONNECT,      // Connect to the MQTT broker for publishing and subscribing to topics
    LTE_STATE_MQTT_SUBSCRIBE,    // Subscribe to topics on the MQTT broker to receive messages

    LTE_STATE_RUN,               // Normal operation state: modem is ready for sending and receiving data

    LTE_STATE_ERROR,             // Error state: handle errors, reset modem, and retry initialization
    LTE_STATE_REBOOT_MODEM,      // Reboot the modem in case of critical errors or unresponsive behavior
    LTE_STATE_REATTACH,          // Reattach to the network if the modem loses connection or registration
    LTE_STATE_REPDP,             // Reactivate the PDP context if it becomes deactivated or encounters issues
    LTE_STATE_REOPEN_SOCKET,     // Reopen the TCP/UDP socket if it gets closed or encounters errors during communication

    LTE_STATE_MQTT_RECONNECT,    // Reconnect to the MQTT broker if the connection is lost or encounters errors
    LTE_STATE_MQTT_RESUBSCRIBE   // Resubscribe to topics on the MQTT broker after reconnecting
} lte_state_t;

static lte_state_t lte_state = LTE_STATE_INIT;
static uint32_t state_timestamp = 0;
static uint32_t last_pdp_check = 0;

// Server configuration for TCP socket
const char *server_ip = "52.23.44.10"; // Replace with actual server IP address
int port_number = 5000;                // Replace with actual server port number


// Define thresholds for water level and pressure to trigger alarms. 
// These constants are used in the application logic to determine if an alarm condition
//  exists based on sensor readings. The MAX_LEVEL constant represents the maximum allowable
//  water level, while the MIN_PRESSURE constant represents the minimum allowable pressure.
// If the water level exceeds MAX_LEVEL or the pressure drops below MIN_PRESSURE, 
// an alarm will be triggered.   

#define MAX_LEVEL 80.0f 
#define MIN_PRESSURE 30.0f  

/**
 * @brief Initializes the system peripherals and LTE modem.
 * This function sets up the UART for communication with the LTE modem, configures
 * the GPIO pins for controlling the modem's power and reset lines, initializes
 * the timer for state management, and calls the LTE modem initialization routine.
 */
void system_init(void)
{
    uart_init(LTE_UART, 115200);
    gpio_init(LTE_PWRKEY_PIN, GPIO_OUTPUT);
    gpio_init(LTE_RESET_PIN, GPIO_OUTPUT);
    timer_init();
    lte_modem_init();
}

// Main entry point of the program. This function initializes the system and enters
// an infinite loop where it continuously polls the LTE modem for incoming data.
// No RTOS is used, so the state machine and application logic are executed in a
// cooperative manner.
// The main loop consists of the following steps:
// 1. Handle UART RX from modem (non‑blocking): This step checks for any
//    incoming data from the LTE modem via UART and processes it accordingly.
//    It ensures that the modem's responses and indications are handled in a timely manner.
// 2. Run LTE state machine: This step executes the LTE state machine, which manages
//    the various states of the modem, including initialization, network attachment,
//    PDP activation, and socket/MQTT management. It transitions between states based on
//    modem responses and timeouts, ensuring that the modem is properly configured and
//    connected before entering the RUN state for normal operation.
// 3. Run application logic (only when LTE_STATE_RUN): This step executes the main
//    application logic, which is only performed when the LTE modem is in the RUN state.
//    It can include tasks such as sending telemetry, checking for incoming data, and
//    handling other application-specific logic.

int main(void)
{   
    // Initialize system peripherals and LTE modem  
    // This includes setting up UART, GPIOs, timers, 
    // and calling the LTE modem initialization routine.  

    system_init();

    while (1) {
        // 1. Handle UART RX from modem (non‑blocking)
        // Poll UART RX:
        // - Non-blocking
        // - Parses modem responses
        // - Handles unsolicited messages (+TCPRECV, +MQTTPUB)
        lte_modem_poll_rx();

        // 2. Run LTE state machine
        // The LTE state machine manages the various states of the modem, including initialization,
        // network attachment, PDP activation, and socket/MQTT management. It transitions between       

        lte_state_machine();

        // 3. Run application logic (only when LTE_STATE_RUN)
        // The application logic is executed only when the LTE modem is in the RUN state,
        // indicating that the modem is fully initialized, connected to the network, and ready for data     
       // transmission. It can include tasks such as sending telemetry, checking for incoming data,
       // and handling other application-specific logic.       

        app_main_loop();

        // 4. Optional: small delay or idle
        // delay_ms(10);
    }
}

/**
 * @brief LTE state machine implementation.
 * This function manages the various states of the LTE modem, including initialization,
 * power on, boot waiting, SIM checking, network attachment, PDP activation, socket opening,
 * MQTT connection/subscription, and normal operation. It transitions between states based
 * on modem responses and timeouts, ensuring that the modem is properly configured and
 * connected before entering the RUN state for normal operation. In case of errors, it
 * handles retries and resets as necessary.
 *
 * States:
 * - LTE_STATE_INIT: Initializes the modem and sets up GPIO pins.
 * - LTE_STATE_POWER_ON: Toggles the PWRKEY pin to power on the modem.
 * - LTE_STATE_WAIT_BOOT: Waits for the modem to respond to "AT" command.
 * - LTE_STATE_CHECK_SIM: Checks if the SIM card is ready.
 * - LTE_STATE_ATTACH_NETWORK: Attaches to the LTE network and checks registration.
 * - LTE_STATE_PDP_ACTIVATE: Activates the PDP context for data communication.
 * - LTE_STATE_OPEN_SOCKET: Opens a TCP socket to the specified server.
 * - LTE_STATE_MQTT_CONNECT: Connects to MQTT broker.
 * - LTE_STATE_MQTT_SUBSCRIBE: Subscribes to MQTT topics.
 * - LTE_STATE_RUN: Normal operation state where the application can send and receive data.
 * - LTE_STATE_ERROR: Handles errors, resets the modem, and retries initialization.
 */
void lte_state_machine(void)
{
    switch (lte_state) {

    case LTE_STATE_INIT:
        // Power off modem, set pins
        gpio_write(LTE_PWRKEY_PIN, 0); // Ensure PWRKEY is low
        gpio_write(LTE_RESET_PIN, 1);  // Ensure RESET is high (not resetting)
        state_timestamp = timer_ms();  // Record timestamp for state transitions
        lte_state = LTE_STATE_POWER_ON; // Move to power on state
        break;

    // Power on the modem by toggling the PWRKEY pin. The modem requires a specific
    // sequence to power on, which involves holding the PWRKEY high for a certain time.
    case LTE_STATE_POWER_ON:
        // Toggle PWRKEY to start modem
        gpio_write(LTE_PWRKEY_PIN, 1); // Set PWRKEY high to initiate power on sequence
        // hold for e.g. 500 ms
        if (timer_ms() - state_timestamp > 500) {
            gpio_write(LTE_PWRKEY_PIN, 0);
            state_timestamp = timer_ms();
            lte_state = LTE_STATE_WAIT_BOOT;
        }
        break;

    case LTE_STATE_WAIT_BOOT:
        // Wait for modem to respond to "AT"
        // The modem may take a few seconds (e.g. 2 secs) to boot and respond to AT commands.
        // We send an "AT" command and wait for an "OK" response.
        // If the modem does not respond within a timeout period, we transition to an error state.
        if (lte_modem_send_and_wait("AT\r\n", "OK", 2000)) {
            lte_modem_send("ATE0\r\n"); // echo off (fire‑and‑forget)
            // Proceed to check SIM status
            lte_state = LTE_STATE_CHECK_SIM;
        } else if (timer_ms() - state_timestamp > 10000) {
            // Timeout waiting for boot response, transition to error state
            lte_state = LTE_STATE_ERROR;
        }
        break;

    case LTE_STATE_CHECK_SIM:
        if (lte_modem_send_and_wait("AT+CPIN?\r\n", "READY", 3000)) {
            // SIM is ready, proceed to attach to network
            lte_state = LTE_STATE_ATTACH_NETWORK;
        } else {
            // SIM not ready yet, retry later (could add delay or retry counter here)
            lte_state = LTE_STATE_ERROR;
        }
        break;

    // Attach to network and check registration status.
    // The modem must be attached to the LTE network before it can activate a
    // PDP context and open a socket. We send the "AT+CGATT=1" command to
    // attach to the network and then check the registration status using "AT+CEREG?".
    // If the modem is successfully registered, we transition to the PDP activation state.
    case LTE_STATE_ATTACH_NETWORK:
        if (!lte_modem_send_and_wait("AT+CGATT=1\r\n", "OK", 5000)) {
            // still attaching, or error
            lte_state = LTE_STATE_ERROR;
            break;
        }
        // Check registration
        // +CEREG: 0,1 means registered home network
        // +CEREG: 0,5 means registered roaming
        if (lte_modem_send_and_wait("AT+CEREG?\r\n", "0,1", 5000) ||
            lte_modem_send_and_wait("AT+CEREG?\r\n", "0,5", 5000)) {
            lte_state = LTE_STATE_PDP_ACTIVATE;
        } 
        break;

    case LTE_STATE_PDP_ACTIVATE:
        // Configure PDP context
        // Example: set APN to "your_apn"
        // Replace "your_apn" with actual APN for your network provider
        lte_modem_send_and_wait("AT+CGDCONT=1,\"IP\",\"your_apn\"\r\n", "OK", 3000);
        if (lte_modem_send_and_wait("AT+CGACT=1,1\r\n", "OK", 5000)) {
            // PDP context activated, proceed to open socket
            lte_state = LTE_STATE_OPEN_SOCKET;
        }
        break;

    case LTE_STATE_OPEN_SOCKET:
    {
        // Example: open TCP to server
        // The modem must successfully open a TCP socket to the specified server before
        // entering MQTT connect and then RUN state.

        // 1. Open network stack (some modems require NETOPEN before TCP/MQTT)
        lte_modem_send_and_wait("AT+NETOPEN\r\n", "OK", 5000);

        // 2. Create TCP socket with IP + port
        char create_socket_cmd[128];  // command buffer for creating socket
        char open_socket_cmd[64];     // command buffer for opening socket
        sprintf(create_socket_cmd, "AT+TCPCREATE=\"%s\",%d\r\n", server_ip, port_number);
        sprintf(open_socket_cmd, "AT+TCPOPEN=1\r\n"); // open socket 1

        if (lte_modem_send_and_wait(create_socket_cmd, "OK", 5000) &&
            lte_modem_send_and_wait(open_socket_cmd, "OK", 5000)) {
            // TCP socket open → proceed to MQTT connect
            lte_state = LTE_STATE_MQTT_CONNECT;
        } else {
            lte_state = LTE_STATE_ERROR;
        }
        break;
    }

    case LTE_STATE_MQTT_CONNECT:
        // Connect to the MQTT broker for publishing and subscribing to topics.
        // MQTT connect:
        // - Requires NETOPEN already done
        // - Sets broker URL and port
        // - Opens MQTT session
        if (lte_mqtt_connect("broker.hivemq.com", 1883)) {
            lte_state = LTE_STATE_MQTT_SUBSCRIBE;
        } else {
            lte_state = LTE_STATE_ERROR;
        }
        break;

    case LTE_STATE_MQTT_SUBSCRIBE:
        // Subscribe to topics on the MQTT broker to receive messages.
        // MQTT subscribe:
        // - Required before receiving MQTT messages
        if (lte_mqtt_subscribe("device/commands")) {
            // After successful subscription, go to RUN state
            lte_state = LTE_STATE_RUN;
        } else {
            lte_state = LTE_STATE_ERROR;
        }
        break;

    case LTE_STATE_RUN:
        // RUN state:
        // - TCP send/receive
        // - MQTT publish/receive
        // - Application logic only
        // - No modem configuration here
        // Normal operation: app_main_loop() will handle sending/receiving data.
        // The modem is fully initialized, connected to the network, and ready for data transmission.
        break;

    case LTE_STATE_REBOOT_MODEM:
        // Reboot the modem in case of critical errors or unresponsive behavior
        gpio_write(LTE_RESET_PIN, 0); // Assert RESET
        delay_ms(100);                // Hold reset for 100 ms
        gpio_write(LTE_RESET_PIN, 1); // Deassert RESET
        state_timestamp = timer_ms();
        lte_state = LTE_STATE_WAIT_BOOT; // Go back to boot wait state
        break;

    case LTE_STATE_REATTACH:
        // Reattach to the network if the modem loses connection or registration
        lte_state = LTE_STATE_ATTACH_NETWORK;
        break;

    case LTE_STATE_REPDP:
        // Reactivate the PDP context if it becomes deactivated or encounters issues
        lte_state = LTE_STATE_PDP_ACTIVATE;
        break;

    case LTE_STATE_REOPEN_SOCKET:
        // Reopen the TCP/UDP socket if it gets closed or encounters errors during communication
        lte_state = LTE_STATE_OPEN_SOCKET;
        break;

    case LTE_STATE_MQTT_RECONNECT:
        // Reconnect to the MQTT broker if the connection is lost or encounters errors
        // MQTT reconnect:
        // - Re-establish MQTT connection after LTE recovery
        if (lte_mqtt_connect("broker.hivemq.com", 1883)) {
            lte_state = LTE_STATE_MQTT_RESUBSCRIBE;
        } else {
            lte_state = LTE_STATE_ERROR;
        }
        break;

    case LTE_STATE_MQTT_RESUBSCRIBE:
        // Resubscribe to topics on the MQTT broker after reconnecting
        if (lte_mqtt_subscribe("device/commands")) {
            lte_state = LTE_STATE_RUN;
        } else {
            lte_state = LTE_STATE_ERROR;
        }
        break;

    case LTE_STATE_ERROR:
        // ERROR handling:
        // - Close socket
        // - Deactivate PDP
        // - Detach network
        // - Reboot modem
        // - Reattach
        // - Reactivate PDP
        // - Reopen socket
        // - Reconnect MQTT
        // - Resubscribe MQTT

        // For simplicity here: after a delay, restart full init sequence.
        if (timer_ms() - state_timestamp > 5000) {
            state_timestamp = timer_ms();
            lte_state = LTE_STATE_REBOOT_MODEM;
        }
        break;
    }
}

/**
 * @brief Sends an AT command to the LTE modem and waits for an expected response.
 * This function sends a command string to the modem and then polls for incoming
 * data, checking if the last received line contains the expected response. It
 * continues to poll until either the expected response is found or a timeout
 * occurs.
 *
 * send_and_wait:
 * - Sends AT command
 * - Polls RX until expected response or timeout
 * - Non-blocking (cooperative with main loop)
 * - Used for all modem configuration commands
 *
 * @param cmd The AT command string to send to the modem (e.g., "AT+CGATT=1\r\n").
 * @param expect The expected response string to look for in the modem's output (e.g., "OK").
 * @param timeout_ms The maximum time to wait for the expected response, in milliseconds.
 *
 * @return true if the expected response was received within the timeout period; false otherwise.
 */
bool lte_modem_send_and_wait(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    lte_modem_send(cmd);
    uint32_t start = timer_ms();
    while (timer_ms() - start < timeout_ms) {
        lte_modem_poll_rx();
        if (lte_modem_last_line_contains(expect)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Main application loop that runs when the LTE modem is in the RUN state.
 * This function contains the main logic of the application, which is executed
 * only when the LTE modem is fully initialized, connected to the network, and
 * ready for data transmission. It can include tasks such as sending telemetry,
 * checking for incoming data, and handling other application-specific logic.
 *
 * app_main_loop:
 * - Only runs when LTE_STATE_RUN
 * - Handles periodic TCP send
 * - Handles periodic MQTT publish
 * - Handles incoming TCP data
 * - Handles incoming MQTT messages
 * - If any send fails → LTE_STATE_ERROR
 *
 * @note This function should be called repeatedly in the main loop of the program.
 */
void app_main_loop(void)
{
    // Only run application logic when LTE is in RUN state.
    // This ensures that the modem is fully initialized, connected to the network,
    // and ready for data transmission.
    if (lte_state != LTE_STATE_RUN) {
        return; // modem is not fully ready yet.
    }

    // 1. Poll for incoming data from the modem (non-blocking)
    // This function polls the modem for any incoming data or indications, such as
    // +TCPRECV or +MQTTPUB. It should be called frequently to ensure that incoming
    // data is processed.
    lte_modem_poll_rx();

    // 2. TCP periodic send
    static uint32_t last_tcp = 0;
    if (timer_ms() - last_tcp > 10000) {
        if (!lte_tcp_send_data("HELLO_TCP")) {
            lte_state = LTE_STATE_ERROR;
            return;
        }
        last_tcp = timer_ms();
    }

    // 3. MQTT periodic publish
    static uint32_t last_mqtt = 0;
    if (timer_ms() - last_mqtt > 15000) {
        if (!lte_mqtt_publish("device/telemetry", "HELLO_MQTT")) {
            lte_state = LTE_STATE_ERROR;
            return;
        }
        last_mqtt = timer_ms();
    }

    // 4. TCP receive
    char tcp_buf[128];
    int tcp_len = lte_tcp_receive(tcp_buf, sizeof(tcp_buf));
    if (tcp_len > 0) {
        // handle TCP message
        // For demonstration, we simply print it.
        // In a real application, you would process the received data according to
        // your protocol or application logic.
        printf("Received TCP: %.*s\n", tcp_len, tcp_buf);
    }

    // 5. MQTT receive
    char topic[64];
    char msg[128];
    if (lte_mqtt_receive(topic, msg)) {
        // handle MQTT message
        printf("Received MQTT: topic=%s, msg=%s\n", topic, msg);
    }

    // 6. Read sensors
    // In a real application, you would replace these function calls with actual
    // sensor reading logic. For demonstration purposes, we assume that the functions
    // return placeholder values.   
    float water_level = read_water_level_sensor();
    bool leak = read_leak_sensor();
    float pressure = read_pressure_sensor();

    // 7. Alarm logic
    // The alarm logic checks the sensor readings against predefined thresholds to determine
    // if an alarm condition exists. If the water level exceeds MAX_LEVEL, a leak is detected, or the pressure drops below MIN_PRESSURE, an alarm is triggered. The logic also ensures that an alarm is only sent on the transition from a non-alarm state to an alarm state

    static bool alarm_prev = false;
    bool alarm_now = (water_level > MAX_LEVEL) || leak || (pressure < MIN_PRESSURE);

    // 8. On alarm edge (OFF -> ON), build payload and send
    // The alarm payload is constructed with the current sensor readings and sent over both TCP and MQTT. If either send operation fails, the LTE state machine transitions to the ERROR state to handle the failure.   
    
    if (alarm_now && !alarm_prev) {
        char payload[128];
        sprintf(payload, "ALARM:LEVEL=%.1f;LEAK=%d;PRESS=%.2f",
                water_level, leak ? 1 : 0, pressure);

        if (!lte_tcp_send_data(payload) || !lte_mqtt_publish("water/alarms", payload)) {
            lte_state = LTE_STATE_ERROR;
            return;
        }
    }
    // Update previous alarm state for next iteration
    alarm_prev = alarm_now;
    

    // 9. Check PDP health periodically 
    last_pdp_check = 0; // Initialize last_pdp_check to 0 at the start of the application
    if (timer_ms() - last_pdp_check > 30000) { // Check PDP health every 30 seconds
        last_pdp_check = timer_ms();
        if (!lte_check_pdp_health()) {
            lte_state = LTE_STATE_ERROR;
            return;
        }
    }
    
}

/**
 * @brief Sends data over a TCP connection using the LTE modem.
 * This function formats the data into an AT command and sends it to the modem.
 * It waits for an "OK" response to confirm that the data was sent successfully.
 *
 * lte_tcp_send_data:
 * - Sends data over existing TCP socket
 * - Requires socket to be open
 * - Returns false on failure
 *
 * @note Socket must be opened and connected before calling this function.
 *
 * @param data The string data to send over the TCP connection.
 * @return true if the data was sent successfully; false otherwise.
 */
bool lte_tcp_send_data(const char *data)
{
    char cmd[256];
    int len = strlen(data);

    // AT+TCPSEND=<len>,"<payload>"
    sprintf(cmd, "AT+TCPSEND=%d,\"%s\"\r\n", len, data);

    if (!lte_modem_send_and_wait(cmd, "OK", 5000)) {
        return false;   // send failed
    }

    return true;
}

/**
 * @brief Receives data from a TCP connection using the LTE modem.
 * This function checks if the last line received from the modem starts with
 * "+TCPRECV", indicating that there is incoming TCP data. It then parses the
 * length and payload from the response and copies it to the provided output buffer.
 *
 * lte_tcp_receive:
 * - Parses +TCPRECV unsolicited message
 * - Copies payload into buffer
 * - Returns number of bytes received
 *
 * @param out A pointer to the buffer where the received data will be stored.
 * @param max_len The maximum length of data that can be stored in the output buffer.
 * @return The number of bytes received and copied to the output buffer, or 0 if no data was received.
 */
int lte_tcp_receive(char *out, int max_len)
{
    if (lte_modem_last_line_starts_with("+TCPRECV")) {

        // Example: +TCPRECV: 12,"HELLO WORLD"
        const char *line = lte_modem_get_last_line();

        int len = 0;
        char payload[256];

        sscanf(line, "+TCPRECV: %d,\"%255[^\"]\"", &len, payload);

        if (len > max_len) len = max_len;

        memcpy(out, payload, len);
        return len;
    }

    return 0; // no data
}

/**
 * @brief Connects to an MQTT broker using the LTE modem.
 * This function sets the MQTT broker URL and port, then sends the command to
 * connect to the broker. It waits for an "OK" response to confirm that the
 * connection was successful.
 *
 * lte_mqtt_connect:
 * - Sets broker URL
 * - Opens MQTT session
 * - Requires NETOPEN already done
 *
 * @param host The hostname or IP address of the MQTT broker.
 * @param port The port number of the MQTT broker.
 * @return true if the connection was successful; false otherwise.
 */
bool lte_mqtt_connect(const char *host, int port)
{
    char cmd[128];

    // Set MQTT broker URL and port
    sprintf(cmd, "AT+MQTTSETURL=\"%s\",%d\r\n", host, port);
    if (!lte_modem_send_and_wait(cmd, "OK", 3000))
        return false;

    // Connect to MQTT broker
    if (!lte_modem_send_and_wait("AT+MQTTCONN\r\n", "OK", 5000))
        return false;

    return true;
}

/**
 * @brief Publishes a message to an MQTT topic using the LTE modem.
 * This function formats the MQTT publish command with the specified topic and payload,
 * then sends it to the modem. It waits for an "OK" response to confirm that the
 * message was published successfully.
 *
 * lte_mqtt_publish:
 * - Publishes payload to topic
 * - Requires MQTT connection
 *
 * @param topic The MQTT topic to publish the message to.
 * @param payload The message payload to publish.
 * @return true if the message was published successfully; false otherwise.
 */
bool lte_mqtt_publish(const char *topic, const char *payload)
{
    char cmd[256];
    sprintf(cmd, "AT+MQTTPUB=\"%s\",\"%s\",0,0\r\n", topic, payload);

    if (!lte_modem_send_and_wait(cmd, "OK", 5000))
        return false;

    return true;
}

/**
 * @brief Subscribes to an MQTT topic using the LTE modem.
 * This function formats the MQTT subscribe command with the specified topic,
 * then sends it to the modem. It waits for an "OK" response to confirm that the
 * subscription was successful.
 *
 * lte_mqtt_subscribe:
 * - Subscribes to topic
 * - Required before receiving messages
 *
 * @param topic The MQTT topic to subscribe to.
 * @return true if the subscription was successful; false otherwise.
 */
bool lte_mqtt_subscribe(const char *topic)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTSUB=\"%s\",0\r\n", topic);

    if (!lte_modem_send_and_wait(cmd, "OK", 5000))
        return false;

    return true;
}

/**
 * @brief Receives an MQTT message from the LTE modem.
 * This function checks if the last line received from the modem starts with
 * "+MQTTPUB", indicating that there is an incoming MQTT message. It then parses
 * the topic and payload from the response and copies them to the provided buffers.
 *
 * lte_mqtt_receive:
 * - Parses +MQTTPUB unsolicited message
 * - Extracts topic + payload
 *
 * @param topic A pointer to the buffer where the received topic will be stored.
 * @param payload A pointer to the buffer where the received payload will be stored.
 * @return 1 if a message was received and copied to the buffers; 0 if no message was received.
 */
int lte_mqtt_receive(char *topic, char *payload)
{
    if (lte_modem_last_line_starts_with("+MQTTPUB")) {

        const char *line = lte_modem_get_last_line();

        // Example: +MQTTPUB: "cmd","TURN_ON"
        // Limit lengths to avoid buffer overflow
        sscanf(line, "+MQTTPUB: \"%63[^\"]\",\"%127[^\"]\"", topic, payload);

        return 1; // message received
    }

    return 0; // no MQTT message

}

/**
 * @brief Checks the health of the PDP context and network attachment.
 * This function sends AT commands to the LTE modem to verify that it is attached
 * to the network, that the PDP context is active, and optionally checks registration
 * status. It returns true if all checks pass, indicating that the modem is healthy
 * and ready for data communication; otherwise, it returns false.
 *
 * lte_check_pdp_health:
 * - Checks attach status
 * - Checks PDP activation
 * - Optionally checks registration status
 *
 * @return true if the PDP context is healthy and the modem is attached; false otherwise.
 */

bool lte_check_pdp_health(void)
{
    // Check attach
    if (!lte_modem_send_and_wait("AT+CGATT?\r\n", "1", 2000)) {
        // Not attached → PDP effectively gone
        return false;
    }

    // Check PDP activation
    if (!lte_modem_send_and_wait("AT+CGACT?\r\n", "1,1", 2000)) {
        // Context 1 not active
        return false;
    }

    // Optional: check registration
    if (!lte_modem_send_and_wait("AT+CEREG?\r\n", "0,1", 2000) &&
        !lte_modem_send_and_wait("AT+CEREG?\r\n", "0,5", 2000)) {
        // Not registered home/roaming
        return false;
    }

    return true;
}
