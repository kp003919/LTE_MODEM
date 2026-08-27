#ifndef LTE_TCP_H
#define LTE_TCP_H

#include <stdbool.h>

#define SERVER_IP "52.23.44.10"
#define SERVER_PORT 5000

/** @brief Open a TCP connection.
 * @return true if successful, false otherwise.
 */
bool lte_tcp_open(void);

/** @brief Send data over the TCP connection.
 * @param data Pointer to the data to send.
 * @return true if successful, false otherwise.
 */
bool lte_tcp_send(const char *data);

/** @brief Receive data from the TCP connection.
 * @param out Pointer to the buffer to store received data.
 * @param max_len Maximum length of the buffer.
 * @return Number of bytes received, or -1 on error.
 */
int  lte_tcp_receive(char *out, int max_len);

#endif /* LTE_TCP_H */
