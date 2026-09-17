#ifndef LTE_MQTT_H
#define LTE_MQTT_H

#include <stdbool.h>
#include "lte_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to MQTT broker */
bool lte_mqtt_connect(const char *host, int port);

/* Publish message */
bool lte_mqtt_publish(const char *topic, const char *payload);

/* Subscribe to topic */
bool lte_mqtt_subscribe(const char *topic);

/* Receive MQTT publish: returns 1 if parsed, 0 otherwise */
int lte_mqtt_receive(char *topic, char *payload);

#ifdef __cplusplus
}
#endif

#endif /* LTE_MQTT_H */
