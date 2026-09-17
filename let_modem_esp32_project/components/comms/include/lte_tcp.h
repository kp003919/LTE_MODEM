#ifndef LTE_TCP_H
#define LTE_TCP_H

#include <stdbool.h>
#include "lte_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open TCP socket */
bool lte_tcp_open(void);

/* Send data */
bool lte_tcp_send(const char *data);

/* Receive TCP payload */
int lte_tcp_receive(char *out, int max_len);

/* Close TCP socket (optional, if you implement it later) */
void lte_tcp_close(void);

#ifdef __cplusplus
}
#endif

#endif /* LTE_TCP_H */
