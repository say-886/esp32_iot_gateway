#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

typedef struct {
    float temperature;
    float humidity;
    float light;
    bool led_on;
    bool buzzer_on;
    bool relay_on;
    bool wifi_connected;
    bool mqtt_connected;
    uint32_t uptime_sec;
    uint32_t error_code;
    char firmware_version[16];
    device_state_t state;
} device_status_t;

typedef struct {
    bool led_set;
    bool led_value;
    bool buzzer_set;
    bool buzzer_value;
    bool relay_set;
    bool relay_value;
} device_cmd_t;

#endif
