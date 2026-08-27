#ifndef LTE_MQTT_H
#define LTE_MQTT_H

#include <stdbool.h>

bool lte_mqtt_connect(const char *host, int port);
bool lte_mqtt_publish(const char *topic, const char *payload);
bool lte_mqtt_subscribe(const char *topic);
int  lte_mqtt_receive(char *topic, char *payload);

#endif /* LTE_MQTT_H */
