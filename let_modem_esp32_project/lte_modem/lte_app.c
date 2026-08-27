#include "lte_app.h"
#include "lte_sm.h"
#include "lte_tcp.h"
#include "lte_mqtt.h"
#include "lte_hw.h"
#include <stdio.h>

/**
 * @brief Main application loop that runs when the LTE modem is in the RUN state.
 * This function contains the main logic of the application, which is executed
 * only when the LTE modem is fully initialized, connected to the network, and
 * ready for data transmission. It can include tasks such as sending telemetry,
 * checking for incoming data, and handling other application-specific logic.
 *
 * lte_app_loop:
 * - Only runs when LTE_SM_RUN
 * - Handles periodic TCP send
 * - Handles periodic MQTT publish
 * - Handles incoming TCP data
 * - Handles incoming MQTT messages
 * - If any send fails → LTE_SM_ERROR
 *
 * @note This function should be called repeatedly in the main loop of the program.
 */

#define MAX_LEVEL     80.0f // Maximum water level threshold for alarm
#define MIN_PRESSURE  30.0f // Minimum pressure threshold for alarm

static uint32_t s_last_tcp  = 0; // Timestamp of the last TCP send operation
static uint32_t s_last_mqtt = 0; // Timestamp of the last MQTT publish operation    

// Mock sensor reading functions for demonstration purposes 
// In a real application, these would interface with actual hardware sensors.   
static float read_water_level_sensor(void)  { return 50.0f; }
static float read_pressure_sensor(void)     { return 40.0f; }
static int   read_leak_sensor(void)         { return 0; }

// Main application loop that runs when the LTE modem is in the RUN state.
// It handles periodic TCP sends, MQTT publishes, and checks sensor readings 
// to trigger alarms if thresholds are exceeded.  

void lte_app_loop(void)
{   
    // Only run application logic when LTE is in RUN state.
    // This ensures that the modem is fully initialized, connected to the network,  
    // and ready for data transmission.
    if (lte_sm_get_state() != LTE_SM_RUN)
    {  // modem is not ready yet, skip application logic
        return;
    }          
        
    // Get the current time in milliseconds for timing operations
    uint32_t now = lte_hw_ms();
    
    // Periodically send a TCP message every 10 seconds
    // This is a simple heartbeat message to demonstrate TCP communication.     
    if (now - s_last_tcp > 10000) {
        lte_tcp_send("HELLO_TCP");
        s_last_tcp = now;
    }
   
    // Periodically publish an MQTT message every 15 seconds
    // This is a simple telemetry message to demonstrate MQTT communication.
    if (now - s_last_mqtt > 15000) {
        lte_mqtt_publish("device/telemetry", "HELLO_MQTT");
        s_last_mqtt = now;
    }
    
    // Read sensor values for water level, pressure, and leak detection
    // In a real application, these would be replaced with actual sensor reading logic.     

    float level    = read_water_level_sensor();
    float pressure = read_pressure_sensor();
    int   leak     = read_leak_sensor();
   
    // Alarm logic: Check if any of the sensor readings exceed predefined thresholds
    // If the water level exceeds MAX_LEVEL, a leak is detected, or the pressure drops
    //  below MIN_PRESSURE, an alarm condition is triggered.     

    static int alarm_prev = 0;
    int alarm_now = (level > MAX_LEVEL) || leak || (pressure < MIN_PRESSURE);

    if (alarm_now && !alarm_prev) {
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "ALARM:LEVEL=%.1f;LEAK=%d;PRESS=%.2f",
                 level, leak, pressure);
        lte_tcp_send(payload);
        lte_mqtt_publish("water/alarms", payload);
    }
   
    // Update the previous alarm state for the next iteration to track changes 
    // in alarm conditions. 
    alarm_prev = alarm_now;
   
}
