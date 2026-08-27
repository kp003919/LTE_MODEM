#include "lte_driver.h"
#include "lte_hw.h"
#include <string.h>
#include <stdint.h>

// Configuration parameters for the LTE driver  

// queue to handle events from the LTE modem (e.g., received lines, timeouts, errors)   

#define LTE_EVENT_QUEUE_SIZE 16      // size of the event queue
#define LTE_LINE_BUF_SIZE    256     // buffer for last received line

/* -------------------------------
   Event queue and last line
   ------------------------------- */

static LteEvent_t s_events[LTE_EVENT_QUEUE_SIZE];
static int        s_head = 0;
static int        s_tail = 0;
static char       s_last_line[LTE_LINE_BUF_SIZE];

/* -------------------------------
   Internal helpers
   ------------------------------- */

/** @brief Push an event into the queue. 
 * 
 *@note This function is used internally to add events to the event queue. 
 * It checks if there is space in the queue before adding the event. 
 * If the queue is full, the event is dropped. 
 * 
 * @param type Event type
 * @param line Pointer to the line (can be NULL)
 * 
 */
static void push_event(LteEventType_t type, const char *line)
{
    int next = (s_head + 1) % LTE_EVENT_QUEUE_SIZE;
    if (next != s_tail) {
        s_events[s_head].type = type;
        s_events[s_head].line = line;
        s_head = next;
    }
}

/* -------------------------------
   Public API
   ------------------------------- */

void lte_driver_init(void)
{
    s_head = s_tail = 0;
    s_last_line[0] = '\0';
}

/* Called by main loop to fetch next event */
int lte_driver_get_next_event(LteEvent_t *ev)
{   
    // Check if the queue is empty
    if (s_head == s_tail) return 0;
    
    // Copy the event to the provided pointer   
    *ev = s_events[s_tail];
    s_tail = (s_tail + 1) % LTE_EVENT_QUEUE_SIZE;
    return 1;
}

/* Send AT command to modem via UART */
void lte_driver_send_cmd(const char *cmd)
{   
    // if no active command, send the command to the modem
    if (!cmd) return;
    lte_hw_uart_write(cmd);
}

/* Check if last line contains substring */
int lte_driver_last_line_contains(const char *substr)
{
    if (!substr) return 0;
    return strstr(s_last_line, substr) != NULL;
}

/* Check if last line starts with prefix */
int lte_driver_last_line_starts_with(const char *prefix)
{
    if (!prefix) return 0;
    size_t lp = strlen(prefix);
    return strncmp(s_last_line, prefix, lp) == 0;
}

/* Get pointer to last received line */
const char *lte_driver_get_last_line(void)
{
    return s_last_line;
}

/* -------------------------------
   UART RX byte handler
   ------------------------------- */
/* Called from UART ISR via lte_hw when a byte is received */

void lte_driver_on_rx_byte(uint8_t b)
{
    static char rx_buf[LTE_LINE_BUF_SIZE];
    static int  rx_pos = 0;

    if (b == '\n') {
        /* terminate line */
        rx_buf[rx_pos] = '\0';

        /* store as last line */
        strncpy(s_last_line, rx_buf, LTE_LINE_BUF_SIZE - 1);
        s_last_line[LTE_LINE_BUF_SIZE - 1] = '\0';

        /* push RX_LINE event */
        push_event(LTE_EVENT_RX_LINE, s_last_line);

        /* reset buffer */
        rx_pos = 0;
    }
    else if (b != '\r') {
        if (rx_pos < LTE_LINE_BUF_SIZE - 1) {
            rx_buf[rx_pos++] = (char)b;
        }
    }
}

/* -------------------------------
   Optional: timeout / error hooks
   ------------------------------- */

void lte_driver_push_timeout_event(void)
{
    push_event(LTE_EVENT_TIMEOUT, NULL);
}

void lte_driver_push_error_event(void)
{
    push_event(LTE_EVENT_ERROR, NULL);
}
