#include "modem_task.h"
#include "gsmr_modem.h"

QueueHandle_t    g_gsmrCommandQueue = NULL;
SemaphoreHandle_t g_gsmrRxSemaphore  = NULL;

static void vModemTask(void *pvParameters)
{
    (void)pvParameters;
    ModemCommand_t cmd;

    gsmr_modem_init_uart();

    for (;;)
    {
        if (xQueueReceive(g_gsmrCommandQueue, &cmd, portMAX_DELAY) == pdTRUE)
        {
            switch (cmd.type)
            {
                case MODEM_CMD_SEND_AT:
                    gsmr_modem_send_at(cmd.pCmd, cmd.length);
                    gsmr_modem_wait_reply();
                    break;

                case MODEM_CMD_SEQUENCE_BASIC:
                    gsmr_modem_basic_checks();
                    break;

                case MODEM_CMD_SEQUENCE_GPRS:
                    gsmr_modem_setup_gprs();
                    break;

                case MODEM_CMD_SEQUENCE_TCP_TEST:
                    gsmr_modem_tcp_test();
                    break;

                case MODEM_CMD_SEQUENCE_UDP_TEST:
                    gsmr_modem_udp_test();
                    break;

                default:
                    break;
            }
        }
    }
}

void ModemTask_Create(void)
{
    g_gsmrCommandQueue = xQueueCreate(10, sizeof(ModemCommand_t));
    g_gsmrRxSemaphore  = xSemaphoreCreateBinary();

    xTaskCreate(vModemTask,
                "GSMR_ModemTask",
                configMINIMAL_STACK_SIZE * 4,
                NULL,
                tskIDLE_PRIORITY + 2,
                NULL);
}
