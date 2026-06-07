#ifndef DEVICE_CONTROL_H
#define DEVICE_CONTROL_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t device_control_init(void);
esp_err_t device_led_set(bool on);
esp_err_t device_buzzer_set(bool on);
esp_err_t device_relay_set(bool on);
bool device_led_get(void);
bool device_buzzer_get(void);
bool device_relay_get(void);

#endif
