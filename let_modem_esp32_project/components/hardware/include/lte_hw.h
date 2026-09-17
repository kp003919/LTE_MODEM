#pragma once
#include <stdint.h>

/** 
*@file describe the LTE hardware interface
* 
*/

/**
 * @brief Initializes the LTE hardware interface, including UART and any other necessary peripherals.
 * This function should be called during system startup to prepare the LTE hardware 
 * for operation. 
 * 
 * @note This function may be extended in the future to include additional 
 *        hardware initialization steps as needed. 
 * 
 */
void lte_hw_init(void);

/**
 * @brief Writes a string of data to the LTE modem via UART.
 * 
 * @param data A null-terminated string to be sent to the LTE modem.
 * 
 * @note This function uses the underlying UART driver to transmit data. 
 *       Ensure that the UART is properly initialized before calling this function.
 */
void lte_hw_uart_write(const char *data);

/**
 * @brief Retrieves the current system time in milliseconds since the system started.
 * 
 * @return The number of milliseconds since the system started.
 * 
 * @note This function relies on a millisecond tick counter that is incremented
 *       by a timer interrupt. Ensure that the timer is configured correctly.
 */
uint32_t lte_hw_ms(void);

/**
 * @brief Handles the reception of a single byte from the LTE modem.
 * 
 * @param byte The byte received from the LTE modem.
 */
void lte_driver_on_rx_byte(uint8_t byte);


/**
 * @brief Executes the power-on sequence for the LTE modem.
 * This function controls the GPIO pins to power on the LTE modem and perform
 * any necessary initialization steps. It should be called during system startup
 * or when the modem needs to be powered on.
 */
void lte_hw_power_on_sequence(void);

/**
 * @brief Resets the LTE modem by toggling the reset pin.
 * This function performs a hardware reset of the LTE modem, ensuring that it
 * returns to a known state. It should be called when the modem is unresponsive
 * or needs to be reinitialized.
 */
void lte_hw_reset_modem(void);

/**
 * @brief Increments the millisecond tick counter.
 * This function should be called from a timer interrupt that fires every 1 ms.
 * It is used to maintain an accurate system time in milliseconds.
 */

void lte_hw_tick_1ms(void);

/**
 * @brief Delays for a specified number of milliseconds.
 * This function provides a simple delay mechanism based on the system tick counter.
 *
 * @param ms The number of milliseconds to delay.
 */
void lte_hw_delay_ms(uint32_t ms);
