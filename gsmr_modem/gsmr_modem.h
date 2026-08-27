#ifndef GSMR_MODEM_H
#define GSMR_MODEM_H

#include <stdint.h>
#include <stddef.h>

/* ===========================
   MODEM CONSTANTS & SETTINGS
   =========================== */

#define GSMR_APN                "comms365.o2.com"
#define GSMR_TCP_SERVER_IP      "149.241.46.177"
#define GSMR_TCP_SERVER_PORT    "50000"
#define GSMR_UDP_SERVER_IP      "172.16.9.101"
#define GSMR_UDP_SERVER_PORT    "50001"

/* ===========================
   BASIC AT COMMANDS
   =========================== */

extern const uint8_t AT_CMD_OK[];
extern const uint8_t AT_CMD_DETAILS[];
extern const uint8_t AT_CMD_SIM_READY[];
extern const uint8_t AT_CMD_SCID[];
extern const uint8_t AT_CMD_CREG[];
extern const uint8_t AT_CMD_CSQ[];

/* ===========================
   PDP / GPRS / NETWORK
   =========================== */

extern const uint8_t AT_CMD_SICS_CONTYPE[];
extern const uint8_t AT_CMD_SICS_APN[];
extern const uint8_t AT_CMD_SISS_CONID[];

extern const uint8_t AT_CMD_CGATT[];
extern const uint8_t AT_CMD_CGDCONT[];
extern const uint8_t AT_CMD_CGACT[];
extern const uint8_t AT_CMD_CGPADDR[];

/* ===========================
   TCP SERVICE (SERVICE 0)
   =========================== */

extern const uint8_t AT_CMD_TCP_SERVICE[];
extern const uint8_t AT_CMD_TCP_ADDRESS[];
extern const uint8_t AT_CMD_TCP_OPEN[];
extern const uint8_t AT_CMD_TCP_WRITE[];
extern const uint8_t AT_CMD_TCP_READ[];
extern const uint8_t AT_CMD_TCP_CLOSE[];

/* ===========================
   UDP SERVICE (SERVICE 1)
   =========================== */

extern const uint8_t AT_CMD_UDP_SERVICE[];
extern const uint8_t AT_CMD_UDP_ADDRESS[];
extern const uint8_t AT_CMD_UDP_OPEN[];
extern const uint8_t AT_CMD_UDP_WRITE[];
extern const uint8_t AT_CMD_UDP_READ[];
extern const uint8_t AT_CMD_UDP_CLOSE[];

/* ===========================
   DIAGNOSTICS / STATUS
   =========================== */

extern const uint8_t AT_CMD_SISO_STATUS[];
extern const uint8_t AT_CMD_SMSO[];
extern const uint8_t AT_CMD_SICI[];
extern const uint8_t AT_CMD_SISI[];
extern const uint8_t AT_CMD_SBC[];
extern const uint8_t AT_CMD_SBV[];
extern const uint8_t AT_CMD_CCLK[];

/* ===========================
   MODEM API PROTOTYPES
   =========================== */

void modem_init_uart(void);
void modem_send_at(const uint8_t *cmd, size_t len);
void modem_wait_reply(void);
void modem_send_ctrl_z(void);

void modem_basic_checks(void);
void modem_setup_gprs(void);
void modem_tcp_test(void);
void modem_udp_test(void);

#endif /* GSMR_MODEM_H */
