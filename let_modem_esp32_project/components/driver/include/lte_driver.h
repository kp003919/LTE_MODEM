#ifndef LTE_DRIVER_H
#define LTE_DRIVER_H

#include "lte_types.h"
#include <stdint.h>

void lte_driver_init(void);
void lte_driver_reset(void);

void lte_driver_send_cmd(const char *cmd);
void lte_driver_poll_rx(void);

void lte_driver_on_rx_byte(uint8_t byte);


int  lte_driver_get_next_event(LteEvent_t *ev);

/* helpers for higher layers */
int  lte_driver_last_line_contains(const char *substr);
int  lte_driver_last_line_starts_with(const char *prefix);
const char *lte_driver_get_last_line(void);

#endif /* LTE_DRIVER_H */
