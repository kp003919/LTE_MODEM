#ifndef MODEM_SM_H
#define MODEM_SM_H

typedef enum
{
    MODEM_SM_IDLE,
    MODEM_SM_INIT,
    MODEM_SM_BASIC,
    MODEM_SM_GPRS,
    MODEM_SM_TCP,
    MODEM_SM_UDP,
    MODEM_SM_ERROR
} ModemSmState_t;

void ModemSm_Init(void);
void ModemSm_RunStep(void);

#endif /* MODEM_SM_H */
