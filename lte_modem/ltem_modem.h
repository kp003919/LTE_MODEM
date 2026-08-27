#ifndef LTEM_MODEM_H
#define LTEM_MODEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* LTE-M / MQTT configuration */
#define LTEM_APN              "your-apn-here"
#define LTEM_MQTT_HOST        "mqtt.example.com"
#define LTEM_MQTT_PORT        1883
#define LTEM_MQTT_CLIENT_ID   "stm32-client"
#define LTEM_MQTT_USERNAME    "user"
#define LTEM_MQTT_PASSWORD    "pass"
#define LTEM_MQTT_TOPIC_TX    "stm32/tx"
#define LTEM_MQTT_TOPIC_RX    "stm32/rx"

/* Public API */

void ltem_modem_init_uart(void);
void ltem_modem_send_at(const char *cmd);
void ltem_modem_wait_reply(void);
void ltem_modem_send_data(const uint8_t *data, size_t len);

/* High-level sequences */

void ltem_modem_basic_checks(void);
void ltem_modem_attach_network(void);
void ltem_modem_setup_pdp(void);
void ltem_modem_mqtt_connect(void);
void ltem_modem_mqtt_publish(const char *topic, const char *payload);
void ltem_modem_mqtt_subscribe(const char *topic);

/* AT command externs (defined in ltem_modem.c) */

/* Basic / network */
extern const char AT_AT[];
extern const char AT_ATE0[];
extern const char AT_CFUN1[];
extern const char AT_CREGQ[];
extern const char AT_CGATT1[];
extern const char AT_COPSQ[];

/* PDP / APN */
extern const char AT_CGDCONT[];
extern const char AT_QIACT[];
extern const char AT_QIACT_QUERY[];

/* MQTT */
extern const char AT_QMTOPEN[];
extern const char AT_QMTCONN[];
extern const char AT_QMTPUB[];
extern const char AT_QMTSUB[];
extern const char AT_QMTDISC[];

#endif /* LTEM_MODEM_H */
