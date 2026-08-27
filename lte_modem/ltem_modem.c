#include "ltem_modem.h"

#include "stm32f4xx_hal.h"
#include "SEGGER_RTT.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "modem_task.h"   /* for g_ltemRxSemaphore */

#define LTEM_UART_HANDLE     huart2   /* extern UART_HandleTypeDef huart2; */
#define LTEM_RTT_CH          0
#define LTEM_RX_BUFFER_LEN   512

extern UART_HandleTypeDef LTEM_UART_HANDLE;

static uint8_t ltem_rxBuffer[LTEM_RX_BUFFER_LEN];
static volatile bool ltem_txDoneFlag = false;

uint32_t ltem_timeout_ms = 5000; /* default timeout for AT command responses */ 

/* ===== AT command definitions ===== */

/* Basic / network */
const char AT_AT[]      = "AT\r\n";
const char AT_ATE0[]    = "ATE0\r\n";
const char AT_CFUN1[]   = "AT+CFUN=1\r\n";
const char AT_CREGQ[]   = "AT+CREG?\r\n";
const char AT_CGATT1[]  = "AT+CGATT=1\r\n";
const char AT_COPSQ[]   = "AT+COPS?\r\n";

/* PDP / APN */
const char AT_CGDCONT[]     = "AT+CGDCONT=1,\"IP\",\"" LTEM_APN "\"\r\n";
const char AT_QIACT[]       = "AT+QIACT=1\r\n";
const char AT_QIACT_QUERY[] = "AT+QIACT?\r\n";

/* MQTT (Quectel-style QMT commands as example) */
const char AT_QMTOPEN[] = "AT+QMTOPEN=0,\"" LTEM_MQTT_HOST "\"," STRINGIFY(LTEM_MQTT_PORT) "\r\n";
const char AT_QMTCONN[] = "AT+QMTCONN=0,\"" LTEM_MQTT_CLIENT_ID "\",\"" LTEM_MQTT_USERNAME "\",\"" LTEM_MQTT_PASSWORD "\"\r\n";
const char AT_QMTPUB[]  = "AT+QMTPUB=0,0,0,\"" LTEM_MQTT_TOPIC_TX "\"\r\n";
const char AT_QMTSUB[]  = "AT+QMTSUB=0,1,\"" LTEM_MQTT_TOPIC_RX "\",0\r\n";
const char AT_QMTDISC[] = "AT+QMTDISC=0\r\n";

/* ===== helpers ===== */

static void ltem_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &LTEM_UART_HANDLE)
    {
        ltem_txDoneFlag = true;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (huart == &LTEM_UART_HANDLE)
    {
        xSemaphoreGiveFromISR(g_ltemRxSemaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ===== public API ===== */

void ltem_modem_init_uart(void)
{
    /* Assume MX_USART2_UART_Init() already called in main.c */
    HAL_UART_Receive_IT(&LTEM_UART_HANDLE, ltem_rxBuffer, sizeof(ltem_rxBuffer));
    SEGGER_RTT_printf(LTEM_RTT_CH, "LTE-M UART initialised\n");
}

void ltem_modem_send_at(const char *cmd)
{
    size_t len = strlen(cmd);
    ltem_txDoneFlag = false;

    HAL_UART_Transmit_IT(&LTEM_UART_HANDLE, (uint8_t *)cmd, len);

    while (!ltem_txDoneFlag)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void ltem_modem_wait_reply(uint32_t timeout_ms)
{
    if (xSemaphoreTake(g_ltemRxSemaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
    {
        SEGGER_RTT_WriteString(LTEM_RTT_CH, (const char *)ltem_rxBuffer);
        SEGGER_RTT_WriteString(LTEM_RTT_CH, "\n-----------------\n");
    }
    else
    {
        SEGGER_RTT_printf(LTEM_RTT_CH, "LTE-M RX timeout\n");
    }
}

void ltem_modem_send_data(const uint8_t *data, size_t len)
{
    ltem_txDoneFlag = false;
    HAL_UART_Transmit_IT(&LTEM_UART_HANDLE, (uint8_t *)data, len);

    while (!ltem_txDoneFlag)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ===== high-level sequences ===== */

void ltem_modem_basic_checks(void)
{
    ltem_modem_send_at(AT_AT);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_ATE0);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_CFUN1);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_CREGQ);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_COPSQ);
    ltem_modem_wait_reply(ltem_timeout_ms);
}

void ltem_modem_attach_network(void)
{
    ltem_modem_send_at(AT_CGATT1);
    ltem_modem_wait_reply(ltem_timeout_ms);
}

void ltem_modem_setup_pdp(void)
{
    ltem_modem_send_at(AT_CGDCONT);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_QIACT);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_QIACT_QUERY);
    ltem_modem_wait_reply(ltem_timeout_ms);
}

void ltem_modem_mqtt_connect(void)
{
    ltem_modem_send_at(AT_QMTOPEN);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_at(AT_QMTCONN);
    ltem_modem_wait_reply(ltem_timeout_ms);
}

void ltem_modem_mqtt_publish(const char *topic, const char *payload)
{
    (void)topic; /* if you want dynamic topic, build command string */
    ltem_modem_send_at(AT_QMTPUB);
    ltem_modem_wait_reply(ltem_timeout_ms);

    ltem_modem_send_data((const uint8_t *)payload, strlen(payload));
    ltem_modem_send_data((const uint8_t *)"\x1A", 1); /* CTRL+Z */
    ltem_modem_wait_reply(ltem_timeout_ms);
}

void ltem_modem_mqtt_subscribe(const char *topic)
{
    (void)topic; /* for dynamic topic, build command string */
    ltem_modem_send_at(AT_QMTSUB);
    ltem_modem_wait_reply(ltem_timeout_ms);
}
