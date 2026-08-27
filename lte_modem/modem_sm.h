#ifndef MODEM_SM_H
#define MODEM_SM_H

typedef enum
{
    LTEM_SM_IDLE,
    LTEM_SM_INIT,
    LTEM_SM_BASIC,
    LTEM_SM_ATTACH,
    LTEM_SM_PDP,
    LTEM_SM_MQTT_CONNECT,
    LTEM_SM_MQTT_READY,
    LTEM_SM_ERROR
} LtemSmState_t;

void LtemSm_Init(void);
void LtemSm_RunStep(void);

#endif /* MODEM_SM_H */
