#ifndef STORAGE_NVS_H
#define STORAGE_NVS_H

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char device_id[32];
    uint32_t sample_period_ms;
} app_config_t;

esp_err_t storage_nvs_init(void);
esp_err_t storage_load_config(app_config_t *config);
esp_err_t storage_save_config(const app_config_t *config);
esp_err_t storage_reset_config(void);

#endif
