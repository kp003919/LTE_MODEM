#include "app_task.h"
#include "modem_task.h"
#include "FreeRTOS.h"
#include "task.h"

void vAppTask(void *pvParameters)
{
    (void)pvParameters;

    ModemCommand_t cmd;

    /* 1. Run GSM‑R basic checks */
    cmd.type   = MODEM_CMD_SEQUENCE_BASIC;
    cmd.pCmd   = NULL;
    cmd.length = 0;
    xQueueSend(g_gsmrCommandQueue, &cmd, portMAX_DELAY);

    /* 2. Setup GPRS / PDP context */
    cmd.type = MODEM_CMD_SEQUENCE_GPRS;
    xQueueSend(g_gsmrCommandQueue, &cmd, portMAX_DELAY);

    /* 3. Run UDP test */
    cmd.type = MODEM_CMD_SEQUENCE_UDP_TEST;
    xQueueSend(g_gsmrCommandQueue, &cmd, portMAX_DELAY);

    /* 4. Run TCP test */
    cmd.type = MODEM_CMD_SEQUENCE_TCP_TEST; 
    xQueueSend(g_gsmrCommandQueue, &cmd, portMAX_DELAY);

    for (;;)
    {
        /* Your application logic here */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
