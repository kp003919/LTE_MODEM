/**
 * Correct TCP module for your LTE state machine
 * - Sets s_current_cmd = LTE_CMD_TCP_OPEN
 * - Sends commands only once
 * - Lets state machine handle OK/ERROR
 * - Matches your forward-path and recovery logic
 */

#include "lte_tcp.h"
#include "lte_driver.h"
#include "lte_hw.h"
#include "lte_sm.h"
#include <stdio.h>
#include <string.h>
#include "lte_types.h"

static LteCommand_t s_current_cmd = LTE_CMD_NONE;
static uint32_t s_state_ts = 0;
#define SERVER_IP   "your.server.ip"
#define SERVER_PORT 1234


/* --------------------------------------------------------------------------
 * TCP OPEN
 *
 * This function ONLY sends the AT commands.
 * It does NOT wait for OK/ERROR.
 * It does NOT parse responses.
 *
 * Your state machine handles:
 *   - OK via lte_cmd_tcp_handle()
 *   - ERROR via lte_cmd_tcp_handle()
 *   - timeout via check_timeout()
 *   - retries via recovery states
 * -------------------------------------------------------------------------- */
bool lte_tcp_open(void)
{
    /* Mark command active */
    s_current_cmd = LTE_CMD_TCP_OPEN;

    /* Create socket */
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+TCPCREATE=\"%s\",%d\r\n",
             SERVER_IP, SERVER_PORT);

    /* Send commands */
    lte_driver_send_cmd("AT+NETOPEN\r\n");
    lte_driver_send_cmd(cmd);
    lte_driver_send_cmd("AT+TCPOPEN=0\r\n");   // socket ID 0 (your modem uses 0)

    /* Timestamp for timeout detection */
    s_state_ts = lte_hw_ms();

    return true;
}

/* --------------------------------------------------------------------------
 * TCP SEND
 *
 * This is already correct.
 * -------------------------------------------------------------------------- */
bool lte_tcp_send(const char *data)
{
    char cmd[256];
    int len = (int)strlen(data);
    
    // Format the AT command to send data over TCP
    snprintf(cmd, sizeof(cmd),
             "AT+TCPSEND=%d,\"%s\"\r\n",
             len, data);
    
    // Send the command to the modem
    lte_driver_send_cmd(cmd);
    return true;
}

/* --------------------------------------------------------------------------
 * TCP RECEIVE
 *
 * Your state machine will call this when RX lines arrive.
 * -------------------------------------------------------------------------- */
int lte_tcp_receive(char *out, int max_len)
{   
    // Get the last line received from the modem
    const char *line = lte_driver_get_last_line();

    if (strncmp(line, "+TCPRECV", 8) == 0) {
        int len = 0;
        char payload[256];
        
        // Parse the received line to extract the length and payload
        sscanf(line, "+TCPRECV: %d,\"%255[^\"]\"", &len, payload);

        if (len > max_len)
            len = max_len;
        // Copy the payload to the output buffer
        memcpy(out, payload, len);
        return len;
    }

    return 0;
}
