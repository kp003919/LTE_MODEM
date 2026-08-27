#ifndef MODEM_TASK_H
#define MODEM_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    MODEM_CMD_SEND_AT,
    MODEM_CMD_SEQUENCE_BASIC,
    MODEM_CMD_SEQUENCE_GPRS,
    MODEM_CMD_SEQUENCE_TCP_TEST,
    MODEM_CMD_SEQUENCE_UDP_TEST
} ModemCommandType_t;

typedef struct
{
    ModemCommandType_t type;
    const uint8_t     *pCmd;
    size_t             length;
} ModemCommand_t;

extern QueueHandle_t   g_gsmrCommandQueue;
extern SemaphoreHandle_t g_gsmrRxSemaphore;

void ModemTask_Create(void);

#endif /* MODEM_TASK_H */
