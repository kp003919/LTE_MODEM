#include "modem_sm.h"
#include "ltem_modem.h"

static LtemSmState_t g_state = LTEM_SM_IDLE;

void LtemSm_Init(void)
{
    g_state = LTEM_SM_INIT;
}

void LtemSm_RunStep(void)
{
    switch (g_state)
    {
        case LTEM_SM_INIT:
            ltem_modem_basic_checks();
            g_state = LTEM_SM_ATTACH;
            break;

        case LTEM_SM_ATTACH:
            ltem_modem_attach_network();
            g_state = LTEM_SM_PDP;
            break;

        case LTEM_SM_PDP:
            ltem_modem_setup_pdp();
            g_state = LTEM_SM_MQTT_CONNECT;
            break;

        case LTEM_SM_MQTT_CONNECT:
            ltem_modem_mqtt_connect();
            g_state = LTEM_SM_MQTT_READY;
            break;

        case LTEM_SM_MQTT_READY:
            /* Here you can periodically publish or wait for app triggers */
            break;

        case LTEM_SM_IDLE:
            /* Wait for external trigger */
            break;

        case LTEM_SM_ERROR:
        default:
            /* Error handling / recovery */
            break;
    }
}
