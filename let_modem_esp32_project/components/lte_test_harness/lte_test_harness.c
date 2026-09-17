#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "lte_sm.h"
#include "lte_driver.h"

int pass = 0;   
int fail = 0;
int SIM_FAILURE_INTERVAL = 5000; 

/**
 * @brief Steps the LTE state machine by processing all queued events and performing a forward-path tick.
 * This function is called repeatedly during testing to ensure that the LTE state machine       
 * processes events and transitions through states as expected. It handles all pending events in the queue  
 * and then invokes the state machine's tick handler to allow it to perform any necessary actions based on its current state.   
 * 
 */

static void step_sm(void){
    LteEvent_t ev;

    /* Process all queued events */
    while (lte_driver_get_next_event(&ev))
        lte_sm_handle_event(&ev);

    /* Forward-path tick */
    lte_sm_handle_event(NULL);
}



/**
 * @brief Injects a line of text into the LTE driver for processing.
 * @param line The line of text to inject.
 */
static void inject_line(const char *line)
{
    printf("[INJECT] %s\n", line);

    for (const char *p = line; *p; p++) {
        lte_driver_on_rx_byte((uint8_t)*p);
    }

    // Proper modem-style line ending
    lte_driver_on_rx_byte('\r');
    lte_driver_on_rx_byte('\n');
    step_sm();
}

/**
 * @brief Runs a test scenario where the LTE state machine successfully transitions through all expected states.
 * This function simulates the normal operation of the LTE modem, including modem boot, SIM readiness, attach,
 * PDP activation, TCP connection, and MQTT connection and subscription. It verifies that the state machine
 * reaches the LTE_SM_RUN state as expected.
 */

static void run_success_path(void)
{
    printf("\n=== SUCCESS PATH TEST ===\n");

    //0. initialize state machine   
    inject_line("OK");  // Initial OK from modem    
    printf("LTE state machine initialized\n");
    
    // 1. Modem boot / initial OK
    inject_line("OK");     
    printf("LTE state machine powered on\n");

    // 2. SIM ready 
    inject_line("+CPIN: READY");  
    inject_line("OK");       // SIM READY   
    printf("LTE state machine SIM ready\n");
    

    // 3. Attach
    inject_line("+CGATT: 1");  
    inject_line("OK");       // ATTACH OK   
    printf("LTE state machine attached\n");  

    // 4. PDP / TCP / MQTT etc. (whatever your SM expects)
    inject_line("OK");   // PDP OK    
    printf("LTE state machine PDP active\n");

    inject_line("OK");    // TCP OK    
    printf("LTE state machine TCP connected\n");

    inject_line("OK"); // MQTT CONNECT OK   
    printf("LTE state machine MQTT connected\n");

    inject_line("OK");  // MQTT SUBSCRIBE OK    
    printf("LTE state machine MQTT subscribed\n");

    if (lte_sm_get_state() == LTE_SM_RUN) {
        printf("LTE state machine successfully reached LTE_SM_RUN\n");
    } else {
        printf("LTE state machine did NOT reach LTE_SM_RUN, current state = %d\n", lte_sm_get_state());
    }   
}

/**
 * @brief Runs a test scenario where the LTE state machine experiences SIM failures before eventually succeeding.
 * This function simulates a situation where the SIM card is initially not ready, causing the state machine to
 * encounter errors. After several failed attempts, the SIM becomes ready, and the state machine successfully
 * transitions through the remaining states to reach LTE_SM_RUN.
 */
static void run_SIM_Failed_Success(void)
{
    printf("\n=== SIM FAILED AND SUCCESSED ===\n");

    //0. initialize state machine   
    inject_line("OK");  // Initial OK from modem    
    printf("LTE state machine initialized\n");
    
    // 1. Modem boot / initial OK
    inject_line("OK");     
    printf("LTE state machine powered on\n");


    // 2. SIM CHECK (FAILED AND SUCCESS)

    // 3 failed SIM checks (NOT READY)
    inject_line("+CPIN: NOT READY");
    inject_line("ERROR");   // 1st failure
    inject_line("+CPIN: NOT READY");
    inject_line("ERROR");   // 2nd failure
    inject_line("+CPIN: NOT READY");
    inject_line("ERROR");   // 3rd failure → should drive LTE_SM_ERROR
    // now inject a successful SIM ready response
    inject_line("+CPIN: READY");  
    inject_line("OK");       // SIM READY   
    printf("LTE state machine SIM ready\n");    

    // 3. Attach
    inject_line("+CGATT: 1");  
    inject_line("OK");       // ATTACH OK   
    printf("LTE state machine attached\n");  

    // 4. PDP / TCP / MQTT etc. (whatever your SM expects)
    inject_line("OK");   // PDP OK    
    printf("LTE state machine PDP active\n");

    inject_line("OK");    // TCP OK    
    printf("LTE state machine TCP connected\n");

    inject_line("OK"); // MQTT CONNECT OK   
    printf("LTE state machine MQTT connected\n");

    inject_line("OK");  // MQTT SUBSCRIBE OK    
    printf("LTE state machine MQTT subscribed\n");

    if (lte_sm_get_state() == LTE_SM_RUN) {
        printf("LTE state machine successfully reached LTE_SM_RUN\n");
    } else {
        printf("LTE state machine did NOT reach LTE_SM_RUN, current state = %d\n", lte_sm_get_state());
    }   
}

/**
 * @brief Runs a test scenario where the LTE state machine recovers from a re-attach failure.
 * This function simulates a situation where the LTE state machine fails to attach to the network,
 * but then successfully reattaches after a failure. It verifies that the state machine can recover
 * from such failures and reach the LTE_SM_RUN state.
 */
static void run_reattach_then_success(void)
{
    printf("\n=== RE-ATTACH (FAIL ONCE → SUCCESS) TEST ===\n");

    inject_line("OK");  
    inject_line("OK");

    inject_line("+CPIN: READY");
    inject_line("OK");
    printf("SIM READY\n");

    // FAIL #1 (correct attach failure)
    inject_line("+CGATT: 0");   // URC: not attached
    inject_line("ERROR");       // command failed
    printf("Attach FAIL #1\n");
    printf("Current state = %d\n", lte_sm_get_state());

    inject_line("+CGATT: 0");   // URC: not attached
    inject_line("ERROR");       // command failed
    printf("Attach FAIL #2\n");
    printf("Current state = %d\n", lte_sm_get_state());

    inject_line("+CGATT: 0");   // URC: not attached
    inject_line("ERROR");       // command failed
    printf("Attach FAIL #3\n");
    printf("Current state = %d\n", lte_sm_get_state());

    // SUCCESS attach
    inject_line("+CGATT: 1");
    inject_line("OK");
    printf("Attach SUCCESS on retry\n");

    inject_line("OK");
    inject_line("OK");
    inject_line("OK");
    inject_line("OK");

    if (lte_sm_get_state() == LTE_SM_RUN)
        printf("LTE SM reached RUN after RE-ATTACH recovery\n");
}


/**
 * @brief Resets the LTE driver and state machine for testing.
 * This function is used to clear all internal state without reinitializing hardware or UART.
 * It resets the event queue, last line buffer, RX buffer, and position, allowing for a clean
 * slate before running test scenarios.
 */

static void reset()
{
   
    lte_driver_init();
    lte_sm_init();
}


    
/* ----------------------------------------------------------
 * Test runner
 * ---------------------------------------------------------- */
void lte_run_all_tests(void)
{
    printf("\n================ MULTI-SCENARIO LTE TESTS ================\n");
    
    // list of runs to execute
    reset();
    run_success_path();    // run the success path test 
    run_reattach_then_success(); // run the re-attach recovery test 
    
    printf("\n================ TESTS COMPLETE ================\n");

}
