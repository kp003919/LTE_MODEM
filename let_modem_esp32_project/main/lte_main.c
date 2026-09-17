#include "lte_driver.h"
#include "lte_sm.h"
#include "lte_app.h"
#include <stdio.h>

#define LTE_TEST_MODE 1



extern void lte_run_all_tests(void);

void app_main(void)
{
#if LTE_TEST_MODE
    printf("Running LTE modem test harness...\n");
    lte_run_all_tests();
    while (1) {}
#else
    LteEvent_t ev;
    printf("Starting LTE modem firmware...\n");

    for (;;)
    {
        /* 1. Poll UART for new modem data */
        //lte_driver_poll_rx();

        /* 2. Drain all pending modem events */
        while (lte_driver_get_next_event(&ev)) {
            lte_sm_handle_event(&ev);
        }

        /* 3. Forward-path tick */
        lte_sm_handle_event(NULL);

        /* 4. Application logic */
        lte_app_loop();
    }
#endif
}
