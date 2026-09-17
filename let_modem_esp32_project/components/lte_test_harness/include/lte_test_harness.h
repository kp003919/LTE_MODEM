
/**
 * @file lte_test_harness.h
 * @brief Header file for LTE test harness functions.
 */

#ifndef LTE_TEST_HARNESS_H
#define LTE_TEST_HARNESS_H


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Runs all LTE modem tests.
 *
 * This function executes a series of tests to verify the functionality of the LTE modem.
 * It is intended for use in a test environment and should be called during system startup
 * or when testing the LTE modem's capabilities.
 *
 * @note This function may block execution while tests are running. Ensure that it is called
 *       in an appropriate context where blocking is acceptable.
 */

void lte_run_all_tests(void);   

#endif // LTE_TEST_HARNESS_H