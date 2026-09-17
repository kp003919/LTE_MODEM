#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "lte_sm.h"
#include "lte_driver.h"
#include "lte_hw.h"

/* --------------------------------------------------------------------------
 * LTE Test Harness
 *
 * This file contains unit tests for the LTE modem state machine and driver.
 * It simulates modem responses and checks that the state machine transitions
 * correctly through various scenarios, including successful attach, PDP context
 * activation, socket opening, and error handling.
 *
 * The tests are designed to be run in a controlled environment where the LTE
 * hardware layer can be mocked or simulated.
 * 
 * Note: The tests assume that the LTE driver and state machine are initialized
 * and that the hardware layer provides a way to simulate time and modem responses. 
 * -------------------------------------------------------------------------- */

/* Simple helper: get current state (add this in lte_sm.c if not present) */
extern LteSmState_t lte_sm_get_state(void);

/* -------------------------------
   Test event injection helpers
   ------------------------------- */

/* Inject a full modem line as if it came from UART */
static void inject_line(const char *line)
{
    size_t len = strlen(line);

    for (size_t i = 0; i < len; i++)
        lte_driver_on_rx_byte((uint8_t)line[i]);

    /* Modem lines are usually terminated with \r\n */
    lte_driver_on_rx_byte('\r');
    lte_driver_on_rx_byte('\n');
}

/* Run one iteration of the state machine:
 * - drain events from driver
 * - feed them to state machine
 * - run forward path (NULL event)
 */
static void step_sm(void)
{
    LteEvent_t ev;

    while (lte_driver_get_next_event(&ev))
        lte_sm_handle_event(&ev);

    /* Forward path (no event) */
    lte_sm_handle_event(NULL);
}

/* Advance "time" for timeout tests (if lte_hw_ms uses a fake tick in test) */
static void advance_time_ms(uint32_t ms)
{
    uint32_t start = lte_hw_ms();
    while ((lte_hw_ms() - start) < ms) {
        step_sm();
    }
}

/* -------------------------------
   Individual test cases
   ------------------------------- */

static int test_attach_ok(void)
{
    printf("TEST: Attach OK -> PDP\n");

    /* Simulate modem responses for attach */
    inject_line("+CGATT:1");
    step_sm();

    inject_line("OK");
    step_sm();

    LteSmState_t st = lte_sm_get_state();
    if (st == LTE_SM_PDP) {
        printf("PASS: state = LTE_SM_PDP\n");
        return 1;
    } else {
        printf("FAIL: state = %d (expected LTE_SM_PDP)\n", st);
        return 0;
    }
}

static int test_attach_error_retry(void)
{
    printf("TEST: Attach ERROR -> REATTACH\n");

    inject_line("+CGATT:0");
    step_sm();

    inject_line("ERROR");
    step_sm();

    LteSmState_t st = lte_sm_get_state();
    if (st == LTE_SM_REATTACH || st == LTE_SM_ERROR) {
        printf("PASS: state = %d (REATTACH/ERROR acceptable)\n", st);
        return 1;
    } else {
        printf("FAIL: state = %d (expected REATTACH/ERROR)\n", st);
        return 0;
    }
}

static int test_pdp_ok(void)
{
    printf("TEST: PDP OK -> OPEN_SOCKET\n");

    inject_line("OK");  /* Assume we are in LTE_SM_PDP and modem replies OK */
    step_sm();

    LteSmState_t st = lte_sm_get_state();
    if (st == LTE_SM_OPEN_SOCKET) {
        printf("PASS: state = LTE_SM_OPEN_SOCKET\n");
        return 1;
    } else {
        printf("FAIL: state = %d (expected LTE_SM_OPEN_SOCKET)\n", st);
        return 0;
    }
}

static int test_cereg_network_lost(void)
{
    printf("TEST: +CEREG:0 -> ERROR/REATTACH\n");

    inject_line("+CEREG: 0,0");
    step_sm();

    LteSmState_t st = lte_sm_get_state();
    if (st == LTE_SM_ERROR || st == LTE_SM_REATTACH) {
        printf("PASS: state = %d (ERROR/REATTACH)\n", st);
        return 1;
    } else {
        printf("FAIL: state = %d (expected ERROR/REATTACH)\n", st);
        return 0;
    }
}

static int test_timeout_attach(void)
{
    printf("TEST: Attach timeout -> ERROR\n");

    /* Assume we are in LTE_SM_ATTACH with active command */
    /* Advance time beyond TIMEOUT_ATTACH */
    advance_time_ms(20000);  /* 20 seconds */

    LteSmState_t st = lte_sm_get_state();
    if (st == LTE_SM_ERROR) {
        printf("PASS: state = LTE_SM_ERROR (timeout)\n");
        return 1;
    } else {
        printf("FAIL: state = %d (expected LTE_SM_ERROR)\n", st);
        return 0;
    }
}

/* -------------------------------
   Test runner
   ------------------------------- */

void lte_run_all_tests(void)
{
    int pass = 0;
    int total = 0;

    /* Initialize hardware layer in test mode if needed */
    lte_hw_init();

    /* You may want to reset state machine to INIT before each test */

    total++; pass += test_attach_ok();
    total++; pass += test_attach_error_retry();
    total++; pass += test_pdp_ok();
    total++; pass += test_cereg_network_lost();
    total++; pass += test_timeout_attach();

    printf("\nTEST SUMMARY: %d/%d passed\n", pass, total);
}
