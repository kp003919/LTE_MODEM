#include "modem_task.h"
#include "ltem_modem.h"

QueueHandle_t    g_ltemCommandQueue = NULL;
SemaphoreHandle_t g_ltemRxSemaphore  = NULL;

static void vModemTask(void *pvParameters)
{
    (void)pvParameters;
    ModemCommand_t cmd;

    ltem_modem_init_uart();

    for (;;)
    {
        if (xQueueReceive(g_ltemCommandQueue, &cmd, portMAX_DELAY) == pdTRUE)
        {
            switch (cmd.type)
            {
                case MODEM_CMD_LTEM_BASIC:
                    ltem_modem_basic_checks();
                    break;

                case MODEM_CMD_LTEM_ATTACH:
                    ltem_modem_attach_network();
                    break;

                case MODEM_CMD_LTEM_PDP:
                    ltem_modem_setup_pdp();
                    break;

                case MODEM_CMD_LTEM_MQTT_CONNECT:
                    ltem_modem_mqtt_connect();
                    break;

                case MODEM_CMD_LTEM_MQTT_PUBLISH:
                    ltem_modem_mqtt_publish(cmd.topic, cmd.payload);
                    break;

                case MODEM_CMD_LTEM_MQTT_SUBSCRIBE:
                    ltem_modem_mqtt_subscribe(cmd.topic);
                    break;

                default:
                    break;
            }
        }
    }
}

void ModemTask_Create(void)
{
    g_ltemCommandQueue = xQueueCreate(10, sizeof(ModemCommand_t));
    g_ltemRxSemaphore  = xSemaphoreCreateBinary();

    xTaskCreate(vModemTask,
                "LTEM_ModemTask",
                configMINIMAL_STACK_SIZE * 4,
                NULL,
                tskIDLE_PRIORITY + 2,
                NULL);
}
