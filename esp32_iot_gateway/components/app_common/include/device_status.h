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

void device_status_init_default(device_status_t *status);
void device_status_store_init(void);
void device_status_get(device_status_t *status);
void device_status_update_sensor(float temperature, float humidity, float light);
void device_status_update_control(const device_cmd_t *cmd);
void device_status_update_network(bool wifi_connected, bool mqtt_connected);
void device_status_tick(uint32_t seconds);

#endif
