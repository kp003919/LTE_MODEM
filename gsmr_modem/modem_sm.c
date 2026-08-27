#include "modem_sm.h"
#include "gsmr_modem.h"

static ModemSmState_t g_state = MODEM_SM_IDLE;

void ModemSm_Init(void)
{
    g_state = MODEM_SM_INIT;
}

void ModemSm_RunStep(void)
{
    switch (g_state)
    {
        case MODEM_SM_INIT:
            /* UART already init in modem task; could do extra here */
            g_state = MODEM_SM_BASIC;
            break;

        case MODEM_SM_BASIC:
            gsmr_modem_basic_checks();
            g_state = MODEM_SM_GPRS;
            break;

        case MODEM_SM_GPRS:
            gsmr_modem_setup_gprs();
            g_state = MODEM_SM_TCP;
            break;

        case MODEM_SM_TCP:
            gsmr_modem_tcp_test();
            g_state = MODEM_SM_UDP;
            break;

        case MODEM_SM_UDP:
            gsmr_modem_udp_test();
            g_state = MODEM_SM_IDLE;
            break;

        case MODEM_SM_IDLE:
            /* wait for external trigger if needed */
            break;

        case MODEM_SM_ERROR:
        default:
            /* error handling */
            break;
    }
}
