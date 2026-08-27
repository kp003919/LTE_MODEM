/**
 * Correct MQTT module for your LTE state machine
 * - Sets s_current_cmd = LTE_CMD_MQTT_CONNECT / SUBSCRIBE
 * - Sends commands only once
 * - Lets state machine handle OK/ERROR
 * - Matches your forward-path and recovery logic
 */

#include "lte_mqtt.h"
#include "lte_driver.h"
#include "lte_sm.h"
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * MQTT CONNECT
 *
 * This function ONLY sends the AT commands.
 * It does NOT wait for OK/ERROR.
 * It does NOT parse responses.
 *
 * Your state machine handles:
 *   - OK via lte_cmd_mqtt_conn_handle()
 *   - ERROR via lte_cmd_mqtt_conn_handle()
 *   - timeout via check_timeout()
 *   - retries via recovery states
 * -------------------------------------------------------------------------- */
bool lte_mqtt_connect(const char *host, int port)
{
    /* Mark command active */
    s_current_cmd = LTE_CMD_MQTT_CONNECT;

    char cmd[128];
    // Format the AT command to set the MQTT broker URL and port
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTSETURL=\"%s\",%d\r\n",
             host, port);

    /* Send commands */
    lte_driver_send_cmd(cmd);
    lte_driver_send_cmd("AT+MQTTCONN\r\n");

    /* Timestamp for timeout detection */
    s_state_ts = lte_hw_ms();

    return true;
}

/* --------------------------------------------------------------------------
 * MQTT SUBSCRIBE
 *
 * Called by forward path when entering LTE_SM_MQTT_SUBSCRIBE.
 * -------------------------------------------------------------------------- */
bool lte_mqtt_subscribe(const char *topic)
{
    /* Mark command active */
    s_current_cmd = LTE_CMD_MQTT_SUBSCRIBE;
    
    // format the AT command to subscribe to the specified MQTT topic
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTSUB=\"%s\",0\r\n",
             topic);

    lte_driver_send_cmd(cmd);

    /* Timestamp for timeout detection */
    s_state_ts = lte_hw_ms();

    return true;
}

/* --------------------------------------------------------------------------
 * MQTT PUBLISH
 *
 * Called by your application when in LTE_SM_RUN.
 * This does NOT affect state machine.
 * -------------------------------------------------------------------------- */
bool lte_mqtt_publish(const char *topic, const char *payload)
{   
    // format the AT command to publish a message to the specified MQTT topic
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUB=\"%s\",\"%s\",0,0\r\n",
             topic, payload);

    lte_driver_send_cmd(cmd);
    return true;
}

/* --------------------------------------------------------------------------
 * MQTT RECEIVE
 *
 * Called by RX event handler when modem outputs:
 *   +MQTTPUB: "topic","payload"
 * -------------------------------------------------------------------------- */
int lte_mqtt_receive(char *topic, char *payload)
{  
    // Get the last line received from the modem    
    const char *line = lte_driver_get_last_line();
    
    // Check if the line starts with "+MQTTPUB" and parse the topic and payload 
    if (strncmp(line, "+MQTTPUB", 8) == 0) {
        sscanf(line,
               "+MQTTPUB: \"%63[^\"]\",\"%127[^\"]\"",
               topic, payload);
        return 1;
    }

    return 0;
}
