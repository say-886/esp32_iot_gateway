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

/**
 * @brief 初始化 NVS 存储子系统。
 */
esp_err_t storage_nvs_init(void);

/**
 * @brief 从 NVS 加载应用配置，不存在时返回默认配置。
 *
 * @param config 输出参数，接收加载后的配置。
 */
esp_err_t storage_load_config(app_config_t *config);

esp_err_t storage_validate_config(const app_config_t *config);

/**
 * @brief 将应用配置写入 NVS。
 *
 * @param config 输入参数，待保存的配置内容。
 */
esp_err_t storage_save_config(const app_config_t *config);

/**
 * @brief 将配置重置为默认值并保存到 NVS。
 */
esp_err_t storage_reset_config(void);

#endif
