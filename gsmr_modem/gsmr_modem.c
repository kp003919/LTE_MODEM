#include "gsmr_modem.h"

#include "PSL_core.h"
#include "PSL_pmc.h"
#include "PSL_gpio.h"
#include "PSL_wdt.h"
#include "PSL_uart.h"
#include "PSL_systick.h"
#include "SEGGER_RTT.h"

#include <limits.h>

/* ===========================
   INTERNAL DEFINES
   =========================== */

#define GSMR_DMA_TX_CHANNEL   (0u)
#define GSMR_RX_BUFFER_LEN    (1024u)
#define GSMR_RTT_CH           (0)

/* ===========================
   INTERNAL STATE
   =========================== */

static volatile bool gsmr_txDoneFlag = false;
static volatile bool gsmr_rxErrFlag  = false;

static uint8_t gsmr_rxBuffer[GSMR_RX_BUFFER_LEN];
static uint8_t gsmr_rxByte = 0u;

/* Ctrl+Z and CRLF */
static const uint8_t GSMR_CTRL_Z = 0x1A;
static const uint8_t GSMR_CRLF[] = "\r\n";

/* ===========================
   AT COMMAND DEFINITIONS
   =========================== */

/* Basic */
const uint8_t AT_CMD_OK[]          = "AT\r\n";
const uint8_t AT_CMD_DETAILS[]     = "ATI\r\n";
const uint8_t AT_CMD_SIM_READY[]   = "AT+CPIN?\r\n";
const uint8_t AT_CMD_SCID[]        = "AT^SCID\r\n";
const uint8_t AT_CMD_CREG[]        = "AT+CREG?\r\n";
const uint8_t AT_CMD_CSQ[]         = "AT+CSQ\r\n";

/* PDP / GPRS / Network */
const uint8_t AT_CMD_SICS_CONTYPE[] = "AT^SICS=0,conType,GPRS0\r\n";
const uint8_t AT_CMD_SICS_APN[]     = "AT^SICS=0,apn," GSMR_APN "\r\n";
const uint8_t AT_CMD_SISS_CONID[]   = "AT^SISS=0,conId,0\r\n";

const uint8_t AT_CMD_CGATT[]        = "AT+CGATT=1\r\n";
const uint8_t AT_CMD_CGDCONT[]      = "AT+CGDCONT=1,\"IP\",\"" GSMR_APN "\"\r\n";
const uint8_t AT_CMD_CGACT[]        = "AT+CGACT=1,1\r\n";
const uint8_t AT_CMD_CGPADDR[]      = "AT+CGPADDR=1\r\n";

/* TCP */
const uint8_t AT_CMD_TCP_SERVICE[]  = "AT^SISS=0,srvType,\"Socket\"\r\n";
const uint8_t AT_CMD_TCP_ADDRESS[]  = "AT^SISS=0,address,\"socktcp://" GSMR_TCP_SERVER_IP ":" GSMR_TCP_SERVER_PORT "\"\r\n";
const uint8_t AT_CMD_TCP_OPEN[]     = "AT^SISO=0\r\n";
const uint8_t AT_CMD_TCP_WRITE[]    = "AT^SISW=0,100,0,1\r\n";
const uint8_t AT_CMD_TCP_READ[]     = "AT^SISR=0,200\r\n";
const uint8_t AT_CMD_TCP_CLOSE[]    = "AT^SISC=0\r\n";

/* UDP */
const uint8_t AT_CMD_UDP_SERVICE[]  = "AT^SISS=1,srvType,\"Socket\"\r\n";
const uint8_t AT_CMD_UDP_ADDRESS[]  = "AT^SISS=1,address,\"sockudp://" GSMR_UDP_SERVER_IP ":" GSMR_UDP_SERVER_PORT "\"\r\n";
const uint8_t AT_CMD_UDP_OPEN[]     = "AT^SISO=1\r\n";
const uint8_t AT_CMD_UDP_WRITE[]    = "AT^SISW=1,100,0,1\r\n";
const uint8_t AT_CMD_UDP_READ[]     = "AT^SISR=1,200\r\n";
const uint8_t AT_CMD_UDP_CLOSE[]    = "AT^SISC=1\r\n";

/* Diagnostics */
const uint8_t AT_CMD_SISO_STATUS[]  = "AT^SISO?\r\n";
const uint8_t AT_CMD_SMSO[]         = "AT^SMSO\r\n";
const uint8_t AT_CMD_SICI[]         = "AT^SICI?\r\n";
const uint8_t AT_CMD_SISI[]         = "AT^SISI?\r\n";
const uint8_t AT_CMD_SBC[]          = "AT^SBC?\r\n";
const uint8_t AT_CMD_SBV[]          = "AT^SBV\r\n";
const uint8_t AT_CMD_CCLK[]         = "AT+CCLK?\r\n";

/* ===========================
   INTERNAL HELPERS
   =========================== */

static void gsmr_delay_ms(uint32_t ms)
{
    uint32_t start = PSL_systick_GetMilliSecCount();
    while ((PSL_systick_GetMilliSecCount() - start) < ms)
    {
        /* busy wait */
    }
}

static void gsmr_txDone(void)
{
    gsmr_txDoneFlag = true;
}

static void gsmr_txError(void)
{
    gsmr_txDoneFlag = false;
    SEGGER_RTT_printf(GSMR_RTT_CH, "GSMR TX error\n");
}

static void gsmr_rxError(uint8_t flags)
{
    (void)flags;
    gsmr_rxErrFlag = true;
    SEGGER_RTT_printf(GSMR_RTT_CH, "GSMR RX error\n");
}

static void gsmr_drain_rx(void)
{
    gsmr_delay_ms(200);

    uint32_t count = PSL_uart_GetReceivedCount(COM4);
    if ((count > 0u) && (count != UINT_MAX))
    {
        while (PSL_uart_GetReceivedCount(COM4) > 0u)
        {
            if (is_OK(PSL_uart_GetReceivedDataByte(COM4, &gsmr_rxByte)))
            {
                SEGGER_RTT_printf(GSMR_RTT_CH, "%c", gsmr_rxByte);
            }
        }
    }

    SEGGER_RTT_printf(GSMR_RTT_CH, "\n-----------------------------\n");
}

/* ===========================
   PUBLIC API IMPLEMENTATION
   =========================== */

void modem_init_uart(void)
{
    if (!is_OK(PSL_uart_Init(COM4, BR115200))) return;
    if (!is_OK(PSL_uart_SetTransmitMetrics(COM4, GSMR_DMA_TX_CHANNEL, &gsmr_txDone, &gsmr_txError))) return;
    if (!is_OK(PSL_uart_SetReceiveMetrics(COM4, &gsmr_rxBuffer[0], GSMR_RX_BUFFER_LEN, &gsmr_rxError))) return;
    if (!is_OK(PSL_uart_SetReceiveInterrupt(COM4, true))) return;
    if (!is_OK(PSL_uart_SetReceiveEnable(COM4, true))) return;

    SEGGER_RTT_printf(GSMR_RTT_CH, "GSMR UART initialised\n");
}

void modem_send_at(const uint8_t *cmd, size_t len)
{
    gsmr_txDoneFlag = false;
    gsmr_delay_ms(100);

    if (is_OK(PSL_uart_WriteBuffer(COM4, cmd, len)))
    {
        while (!gsmr_txDoneFlag)
        {
            /* wait for TX complete */
        }
    }
}

void modem_wait_reply(void)
{
    gsmr_drain_rx();
}

void modem_send_ctrl_z(void)
{
    modem_send_at(&GSMR_CTRL_Z, sizeof(GSMR_CTRL_Z));
    modem_send_at(GSMR_CRLF, sizeof(GSMR_CRLF));
}

/* ===========================
   HIGH-LEVEL TEST SEQUENCES
   =========================== */

void modem_basic_checks(void)
{
    modem_send_at(AT_CMD_OK, sizeof(AT_CMD_OK));
    modem_wait_reply();

    modem_send_at(AT_CMD_DETAILS, sizeof(AT_CMD_DETAILS));
    modem_wait_reply();

    modem_send_at(AT_CMD_SIM_READY, sizeof(AT_CMD_SIM_READY));
    modem_wait_reply();

    modem_send_at(AT_CMD_SCID, sizeof(AT_CMD_SCID));
    modem_wait_reply();

    modem_send_at(AT_CMD_CREG, sizeof(AT_CMD_CREG));
    modem_wait_reply();

    modem_send_at(AT_CMD_CSQ, sizeof(AT_CMD_CSQ));
    modem_wait_reply();

    modem_send_at(AT_CMD_SBC, sizeof(AT_CMD_SBC));
    modem_wait_reply();

    modem_send_at(AT_CMD_SBV, sizeof(AT_CMD_SBV));
    modem_wait_reply();
}

void modem_setup_gprs(void)
{
    modem_send_at(AT_CMD_SICS_CONTYPE, sizeof(AT_CMD_SICS_CONTYPE));
    modem_wait_reply();

    modem_send_at(AT_CMD_SICS_APN, sizeof(AT_CMD_SICS_APN));
    modem_wait_reply();

    modem_send_at(AT_CMD_SISS_CONID, sizeof(AT_CMD_SISS_CONID));
    modem_wait_reply();

    modem_send_at(AT_CMD_CGATT, sizeof(AT_CMD_CGATT));
    modem_wait_reply();

    modem_send_at(AT_CMD_CGDCONT, sizeof(AT_CMD_CGDCONT));
    modem_wait_reply();

    modem_send_at(AT_CMD_CGACT, sizeof(AT_CMD_CGACT));
    modem_wait_reply();

    modem_send_at(AT_CMD_CGPADDR, sizeof(AT_CMD_CGPADDR));
    modem_wait_reply();
}

void modem_tcp_test(void)
{
    uint8_t packets = 2u;

    modem_send_at(AT_CMD_TCP_SERVICE, sizeof(AT_CMD_TCP_SERVICE));
    modem_wait_reply();

    modem_send_at(AT_CMD_TCP_ADDRESS, sizeof(AT_CMD_TCP_ADDRESS));
    modem_wait_reply();

    modem_send_at(AT_CMD_TCP_OPEN, sizeof(AT_CMD_TCP_OPEN));
    modem_wait_reply();

    while (packets-- > 0u)
    {
        modem_send_at(AT_CMD_TCP_WRITE, sizeof(AT_CMD_TCP_WRITE));
        modem_wait_reply();

        modem_send_at((const uint8_t *)"TCP:Hello from the GSMR\r\n",
                      sizeof("TCP:Hello from the GSMR\r\n"));
        modem_wait_reply();

        modem_send_ctrl_z();
        modem_wait_reply();

        modem_send_at(AT_CMD_TCP_READ, sizeof(AT_CMD_TCP_READ));
        modem_wait_reply();
    }

    modem_send_at(AT_CMD_TCP_CLOSE, sizeof(AT_CMD_TCP_CLOSE));
    modem_wait_reply();
}

void modem_udp_test(void)
{
    uint8_t packets = 2u;

    modem_send_at(AT_CMD_UDP_SERVICE, sizeof(AT_CMD_UDP_SERVICE));
    modem_wait_reply();

    modem_send_at(AT_CMD_UDP_ADDRESS, sizeof(AT_CMD_UDP_ADDRESS));
    modem_wait_reply();

    modem_send_at(AT_CMD_UDP_OPEN, sizeof(AT_CMD_UDP_OPEN));
    modem_wait_reply();

    while (packets-- > 0u)
    {
        modem_send_at(AT_CMD_UDP_WRITE, sizeof(AT_CMD_UDP_WRITE));
        modem_wait_reply();

        modem_send_at((const uint8_t *)"UDP:Hello from the GSMR\r\n",
                      sizeof("UDP:Hello from the GSMR\r\n"));
        modem_wait_reply();

        modem_send_ctrl_z();
        modem_wait_reply();

        modem_send_at(AT_CMD_UDP_READ, sizeof(AT_CMD_UDP_READ));
        modem_wait_reply();
    }

    modem_send_at(AT_CMD_UDP_CLOSE, sizeof(AT_CMD_UDP_CLOSE));
    modem_wait_reply();
}
