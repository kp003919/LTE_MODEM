#include "lte_hw.h"
#include "lte_driver.h"
#include <string.h>
#include <stdint.h>

/** @brief UART Configuration Parameters    
 * This section defines the UART configuration parameters for the LTE modem.
 *  It includes the UART number, baud rate, TX/RX pin numbers, and buffer size. 
 * These parameters are used to initialize the UART interface for communication 
 * with the LTE modem.
 * 
 * @note The UART number and pin assignments may vary based on the specific ESP32 
 * board and LTE modem used. Adjust these parameters as necessary for your hardware
 *  setup.
 * 
 * @note The buffer size should be large enough to accommodate the expected
 *  volume of data received from the modem. A larger buffer size may be necessary       
 * for high-throughput applications or when receiving large responses from the modem.       
 * 
 * @note The baud rate should match the configuration of the LTE modem.     
 * 
 * @note The UART configuration parameters are critical for reliable communication with the LTE modem. Incorrect settings may result in communication errors or data loss.      
 * @note The UART configuration parameters are defined as macros for easy modification and maintenance. Adjust these macros as needed to suit your specific hardware and application requirements.  
 * 
 * 
 * 
 */

 

*/
/* -------------------------------
   UART Configuration Parameters
   ------------------------------- */

#define LTE_UART_NUM        1     // UART number for LTE modem communication    
#define LTE_UART_BAUD       115200 // Baud rate for UART communication with the LTE modem   
#define LTE_UART_TX_PIN     17     // TX pin number for UART communication with the LTE modem 
#define LTE_UART_RX_PIN     16     // RX pin number for UART communication with the LTE modem
#define LTE_UART_BUF_SIZE   2048   // Buffer size for UART communication with the LTE modem

/* GPIO pins for modem control */
#define LTE_PWRKEY_PIN      5  // power key pin for modem   
#define LTE_RESET_PIN       6 // reset pin for modem

/* -------------------------------
   Internal millisecond timer
   ------------------------------- */

static volatile uint32_t s_ms_tick = 0;

/* Called from your system timer ISR every 1 ms */
void lte_hw_tick_1ms(void)
{
    s_ms_tick++;
}

uint32_t lte_hw_ms(void)
{
    return s_ms_tick;
}

/* -------------------------------
   UART RX Interrupt Handler
   ------------------------------- */
// This function is called from the UART ISR when a byte is received. 
// It reads bytes from the UART FIFO and forwards them to the LTE driver for parsing. 
// The driver will handle line assembly and event generation. 

static void IRAM_ATTR lte_uart_rx_isr(void *arg)
{
    uint8_t byte;

    /* Read one byte from UART FIFO */
    while (uart_read_bytes(LTE_UART_NUM, &byte, 1, 0) == 1)
    {
        /* Forward byte to driver for parsing */
        lte_driver_on_rx_byte(byte);
    }
}

/* -------------------------------
   UART Initialization
   ------------------------------- */

void lte_hw_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = LTE_UART_BAUD, // Set baud rate
        .data_bits = UART_DATA_8_BITS, // Set data bits 
        .parity    = UART_PARITY_DISABLE, // Set parity
        .stop_bits = UART_STOP_BITS_1, // Set stop bits
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE // Disable flow control
    };

    uart_param_config(LTE_UART_NUM, &cfg);
    uart_set_pin(LTE_UART_NUM, LTE_UART_TX_PIN, LTE_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uart_driver_install(LTE_UART_NUM,
                        LTE_UART_BUF_SIZE,
                        LTE_UART_BUF_SIZE,
                        0, NULL, 0);

    /* Register RX interrupt handler */
    uart_isr_register(LTE_UART_NUM, lte_uart_rx_isr, NULL);
    uart_enable_rx_intr(LTE_UART_NUM);
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
    /* Configure GPIO pins */
    gpio_set_direction(LTE_PWRKEY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LTE_RESET_PIN, GPIO_MODE_OUTPUT);

    /* Power-on sequence for LTE modem */
    gpio_set_level(LTE_PWRKEY_PIN, 1);
    lte_hw_delay_ms(1000);
    gpio_set_level(LTE_PWRKEY_PIN, 0);

    /* Optional: reset pulse */
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
    /* Any additional hardware init (timers, GPIO, etc.) */
}

/* -------------------------------
   Delay Helper
   ------------------------------- */

void lte_hw_delay_ms(uint32_t ms)
{
    uint32_t start = lte_hw_ms();
    while ((lte_hw_ms() - start) < ms) {
        /* busy wait */
    }
}
