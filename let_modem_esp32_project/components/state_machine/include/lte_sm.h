#ifndef LTE_SM_H
#define LTE_SM_H

/**
 * @file lte_sm.h
 * @brief Header file for LTE state machine functions.
 *
 * This file contains the declarations for the LTE state machine, including
 * initialization, event handling, state accessors, and forward path functions.
 * It provides a standardized interface for managing the LTE modem's state
 * transitions and event processing.
 *
 * @note This file is part of the LTE modem firmware and should be included in
 *       components that interact with the LTE driver and state machine.
 */ 

#include "lte_types.h"
#include <stdint.h>

/**
 * @brief Initializes the LTE state machine.    
 * This function sets the initial state of the LTE state machine and prepares it for operation.
 * It should be called once during system startup before any events are processed.  
 * 
 * @note This function may be extended in the future to include additional
 *       initialization steps as needed.        
 * 
 * 
 */
void lte_sm_init(void);

/* ----------------------------------------------------------
 * MAIN EVENT DISPATCHER
 * ---------------------------------------------------------- */

/**
 * @brief Main state machine handler.
 *
 * @param ev Pointer to event, or NULL for forward-path tick.
 */
void lte_sm_handle_event(const LteEvent_t *ev);

/* ----------------------------------------------------------
 * STATE ACCESSORS
 * ---------------------------------------------------------- */

/* Get current state */
LteSmState_t lte_sm_get_state(void);

/* Set state (runtime use only) */
void lte_sm_set_state(LteSmState_t st);

/* Force state (TEST MODE ONLY) */
void lte_sm_force_state(LteSmState_t st);

/* ----------------------------------------------------------
 * FORWARD PATH (runtime only)
 * ---------------------------------------------------------- */

/**
 * @brief Forward-path tick (runtime only).
 *
 * NOTE: In test mode this is disabled via
 *       lte_sm_disable_forward_path().
 */
void lte_sm_forward_path(void);
void lte_sm_disable_forward_path(void) ;
void lte_sm_disable_boot_sequence(void) ;
void lte_sm_disable_attach_handler(void) ;


#endif /* LTE_SM_H */
