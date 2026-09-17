#include "lte_hw.h"
#include "lte_driver.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdint.h>

/**
 * @file lte_hw.c
 * @brief Implementation of the LTE hardware interface for ESP32.   
 * This file provides the necessary functions to initialize and manage the LTE modem hardware,
 * including UART communication, GPIO control for power and reset, and a millisecond tick counter.  
 * 
 * @note This implementation is specific to the ESP32 platform and may need to be adapted for other hardware.   
 * 
 * To upade for other platforms and MCUs: 
 *   -  Update the UART initialization and configuration to 
 *      match the target platform's UART driver.    
 *   -  Update GPIO control functions to match the target platform's GPIO driver.    
 *   -  Ensure that a millisecond tick counter is implemented, either via a timer 
 *      interrupt or an RTOS tick hook. 
 * 
 */

/* -------------------------------
   UART Configuration Parameters
   ------------------------------- */
#define LTE_UART_NUM        UART_NUM_1  
#define LTE_UART_BAUD       115200
#define LTE_UART_TX_PIN     17
#define LTE_UART_RX_PIN     16
#define LTE_UART_BUF_SIZE   2048


/* GPIO pins for modem control */
#define LTE_PWRKEY_PIN      5
#define LTE_RESET_PIN       6

/* -------------------------------
   Internal millisecond timer
   ------------------------------- */

static volatile uint32_t s_ms_tick = 0;

void lte_hw_tick_1ms(void)
{
    s_ms_tick++;
}

uint32_t lte_hw_ms(void)
{
    return s_ms_tick;
}



void lte_hw_delay_ms(uint32_t ms)
{
    
}

/* -------------------------------
   UART RX Task (instead of ISR)
   ------------------------------- */

static void lte_uart_rx_task(void *arg)
{
    uint8_t byte;

    while (1) {
        int len = uart_read_bytes(LTE_UART_NUM, &byte, 1,
                                  20 / portTICK_PERIOD_MS);
        if (len > 0) {
            lte_driver_on_rx_byte(byte);
        }
    }
}

/* -------------------------------
   UART Initialization
   ------------------------------- */

static void lte_hw_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = LTE_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(LTE_UART_NUM, &cfg);
    uart_set_pin(LTE_UART_NUM,
                 LTE_UART_TX_PIN,
                 LTE_UART_RX_PIN,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);

    uart_driver_install(LTE_UART_NUM,
                        LTE_UART_BUF_SIZE,
                        LTE_UART_BUF_SIZE,
                        0, NULL, 0);

    xTaskCreate(lte_uart_rx_task,
                "lte_uart_rx_task",
                2048,
                NULL,
                10,
                NULL);
}

/* -------------------------------
   UART Write Wrapper
   ------------------------------- */

void lte_hw_uart_write(const char *data)
{
    uart_write_bytes(LTE_UART_NUM, data, strlen(data));
}

/* -------------------------------
   Modem Power Control
   ------------------------------- */

void lte_hw_power_on_sequence(void)
{
    gpio_set_direction(LTE_PWRKEY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LTE_RESET_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LTE_PWRKEY_PIN, 1);
    lte_hw_delay_ms(1000);
    gpio_set_level(LTE_PWRKEY_PIN, 0);

    gpio_set_level(LTE_RESET_PIN, 0);
    lte_hw_delay_ms(200);
    gpio_set_level(LTE_RESET_PIN, 1);
}

/* -------------------------------
   Hardware Initialization
   ------------------------------- */

void lte_hw_init(void)
{
    lte_hw_uart_init();
    /* Add any extra hardware init here if needed */
}
