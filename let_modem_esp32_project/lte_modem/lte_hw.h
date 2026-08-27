#pragma once
#include <stdint.h>

void lte_hw_init(void);
void lte_hw_uart_write(const char *data);

void lte_hw_power_on_sequence(void);
void lte_hw_reset_modem(void);

uint32_t lte_hw_ms(void);
void lte_hw_tick_1ms(void);
void lte_hw_delay_ms(uint32_t ms);
