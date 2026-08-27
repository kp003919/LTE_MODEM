#include "lte_driver.h"
#include "lte_sm.h"
#include "lte_app.h"
#include <stdio.h>

int main(void)
{
    LteEvent_t ev;
    printf("Starting LTE modem firmware...\n");

    for (;;)
    {
        /* --------------------------------------------------------------
         * 1. Poll UART for new modem data
         * -------------------------------------------------------------- */
        lte_driver_poll_rx();   // REQUIRED: generates RX events

        /* --------------------------------------------------------------
         * 2. Drain all pending modem events
         * -------------------------------------------------------------- */
        while (lte_driver_get_next_event(&ev)) {
            lte_sm_handle_event(&ev);
        }

        /* --------------------------------------------------------------
         * 3. Forward-path tick (NULL event)
         *    - Sends AT, attach, PDP, TCP, MQTT
         *    - Does nothing in RUN state
         * -------------------------------------------------------------- */
        lte_sm_handle_event(NULL);

        /* --------------------------------------------------------------
         * 4. Application logic (only runs in RUN state)
         * -------------------------------------------------------------- */
        lte_app_loop();
    }

    return 0;   // never reached
}
