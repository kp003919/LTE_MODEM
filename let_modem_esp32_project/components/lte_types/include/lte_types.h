#ifndef LTE_TYPES_H
#define LTE_TYPES_H

/**
 * @file lte_types.h
 * @brief Header file defining LTE event types, command types, and state machine states.
 *
 * This file contains enumerations and structures used for managing LTE events,
 * commands, and the state machine. It provides a standardized way to represent
 * the various states and events that can occur during LTE modem operation.
 *
 * @note This file is part of the LTE modem firmware and should be included in
 *       components that interact with the LTE driver and state machine.
 */

#include <stdint.h>

// LTE event types, command types, and state machine states are defined below. 

/* LTE event types */

typedef enum {
    LTE_EVENT_NONE = 0,
    LTE_EVENT_RX_LINE,
    LTE_EVENT_TIMEOUT,
    LTE_EVENT_ERROR
} LteEventType_t;

/* LTE command types */

typedef enum {
    LTE_CMD_NONE = 0,
    LTE_CMD_ATTACH,
    LTE_CMD_PDP,
    LTE_CMD_TCP_OPEN,
    LTE_CMD_MQTT_CONNECT,
    LTE_CMD_MQTT_SUBSCRIBE
} LteCommand_t;

/* LTE state machine states */

typedef enum {
    LTE_SM_INVALID = 0,      
    LTE_SM_IDLE,
    LTE_SM_INIT,
    LTE_SM_POWER_ON,
    LTE_SM_WAIT_BOOT,
    LTE_SM_CHECK_SIM,
    LTE_SM_ATTACH,
    LTE_SM_PDP,
    LTE_SM_OPEN_SOCKET,
    LTE_SM_TCP_CONNECT,
    LTE_SM_MQTT_CONNECT,
    LTE_SM_MQTT_SUBSCRIBE,
    LTE_SM_RUN,

    // Recovery / retry states — must match lte_sm.c
    LTE_SM_REATTACH,
    LTE_SM_REPDP,
    LTE_SM_REOPEN_SOCKET,
    LTE_SM_MQTT_RECONNECT,
    LTE_SM_MQTT_RESUBSCRIBE,
    LTE_SM_REBOOT_MODEM,

    LTE_SM_ERROR
} LteSmState_t;

typedef enum {
    LTE_SIM_TEST_SUCCESS_PATH = 0,
    LTE_SIM_TEST_SIM_FAILURE,
    LTE_SIM_TEST_ATTACH_FAILURE,
    LTE_SIM_TEST_PDP_FAILURE,
    LTE_SIM_TEST_REGISTRATION_LOSS,
    LTE_SIM_TEST_TCP_FAILURE,
    LTE_SIM_TEST_MQTT_FAILURE
} LteSimTest_t;

// Change this to select which scenario to run
static LteSimTest_t g_sim_test = LTE_SIM_TEST_SUCCESS_PATH;


/* LTE event structure */

typedef struct {
    LteEventType_t type;
    const char    *line;
} LteEvent_t;



#endif /* LTE_TYPES_H */
