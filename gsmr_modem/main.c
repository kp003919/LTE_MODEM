/* main.c – GSM‑R RTOS System */

#include "PSL_core.h"
#include "PSL_pmc.h"
#include "PSL_gpio.h"
#include "PSL_wdt.h"
#include "PSL_systick.h"
#include "SEGGER_RTT.h"

#include "FreeRTOS.h"
#include "task.h"

#include "modem_task.h"
#include "modem_sm.h"
#include "app_task.h"

/* ===========================
   System Initialization
   =========================== */

static void System_Init(void)
{
    PSL_core_Init();
    PSL_pmc_Init();
    PSL_gpio_Init();
    PSL_wdt_Disable();
    PSL_systick_Init();
    PSL_systick_Enable();

    SEGGER_RTT_Init();
    SEGGER_RTT_WriteString(0,
        "\n=== GSM‑R RTOS System Starting ===\n");
}

/* ===========================
   main()
   =========================== */

int main(void)
{
    /* Hardware + PSL init */
    System_Init();

    /* Create GSM‑R modem RTOS task */
    ModemTask_Create();

    /* Start GSM‑R state machine */
    ModemSm_Init();   // <-- FIXED

    /* Create application task */
    xTaskCreate(vAppTask,
                "AppTask",
                configMINIMAL_STACK_SIZE * 4,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    /* Start FreeRTOS scheduler */
    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {}
}
