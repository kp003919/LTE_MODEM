#ifndef LTE_SM_H
#define LTE_SM_H

#include "lte_types.h"

void lte_sm_init(void);
void lte_sm_handle_event(const LteEvent_t *ev);
LteSmState_t lte_sm_get_state(void);

#endif /* LTE_SM_H */
