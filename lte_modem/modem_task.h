#ifndef MODEM_TASK_H
#define MODEM_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    MODEM_CMD_LTEM_BASIC,
    MODEM_CMD_LTEM_ATTACH,
    MODEM_CMD_LTEM_PDP,
    MODEM_CMD_LTEM_MQTT_CONNECT,
    MODEM_CMD_LTEM_MQTT_PUBLISH,
    MODEM_CMD_LTEM_MQTT_SUBSCRIBE
} ModemCommandType_t;

typedef struct
{
    ModemCommandType_t type;
    const char        *topic;
    const char        *payload;
} ModemCommand_t;

extern QueueHandle_t    g_ltemCommandQueue;
extern SemaphoreHandle_t g_ltemRxSemaphore;

void ModemTask_Create(void);

#endif /* MODEM_TASK_H */
