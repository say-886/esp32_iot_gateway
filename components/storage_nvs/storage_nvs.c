#include "storage_nvs.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("storage_nvs_local.h")
#include "storage_nvs_local.h"
#endif

#define STORAGE_NAMESPACE "gateway"
#define STORAGE_KEY_CONFIG "app_config"

#ifndef APP_DEFAULT_WIFI_SSID
#define APP_DEFAULT_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef APP_DEFAULT_WIFI_PASSWORD
#define APP_DEFAULT_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef APP_DEFAULT_MQTT_HOST
#define APP_DEFAULT_MQTT_HOST "broker.emqx.io"
#endif

#ifndef APP_DEFAULT_MQTT_PORT
#define APP_DEFAULT_MQTT_PORT 1883
#endif

#ifndef APP_DEFAULT_DEVICE_ID
#define APP_DEFAULT_DEVICE_ID "esp32_gateway_001"
#endif

#ifndef APP_DEFAULT_SAMPLE_PERIOD_MS
#define APP_DEFAULT_SAMPLE_PERIOD_MS 2000
#endif

static const app_config_t DEFAULT_CONFIG = {
    .wifi_ssid = APP_DEFAULT_WIFI_SSID,
    .wifi_password = APP_DEFAULT_WIFI_PASSWORD,
    .mqtt_host = APP_DEFAULT_MQTT_HOST,
    .mqtt_port = APP_DEFAULT_MQTT_PORT,
    .device_id = APP_DEFAULT_DEVICE_ID,
    .sample_period_ms = APP_DEFAULT_SAMPLE_PERIOD_MS,
};

static void storage_sanitize_config(app_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->wifi_ssid[sizeof(config->wifi_ssid) - 1] = '\0';
    config->wifi_password[sizeof(config->wifi_password) - 1] = '\0';
    config->mqtt_host[sizeof(config->mqtt_host) - 1] = '\0';
    config->device_id[sizeof(config->device_id) - 1] = '\0';

    if (config->mqtt_host[0] == '\0') {
        memcpy(config->mqtt_host, DEFAULT_CONFIG.mqtt_host, sizeof(config->mqtt_host));
    }
    if (config->mqtt_port == 0) {
        config->mqtt_port = DEFAULT_CONFIG.mqtt_port;
    }
    if (config->device_id[0] == '\0') {
        memcpy(config->device_id, DEFAULT_CONFIG.device_id, sizeof(config->device_id));
    }
    if (config->sample_period_ms < 500 || config->sample_period_ms > 60000) {
        config->sample_period_ms = DEFAULT_CONFIG.sample_period_ms;
    }
}

/**
 * @brief 初始化 NVS Flash，必要时执行擦除恢复。
 *
 * 当分区空间不足或版本不兼容时，会先擦除再重新初始化。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t storage_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/**
 * @brief 从 NVS 读取应用配置，不存在时回退到默认配置。
 *
 * @param config 输出参数，接收读取后的配置内容。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t storage_load_config(app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先填充默认值，避免 NVS 中不存在配置时出现未定义内容。 */
    *config = DEFAULT_CONFIG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t size = sizeof(*config);
    err = nvs_get_blob(handle, STORAGE_KEY_CONFIG, config, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *config = DEFAULT_CONFIG;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        storage_sanitize_config(config);
    }
    return err;
}

/**
 * @brief 将应用配置整体写入 NVS。
 *
 * @param config 输入参数，待保存的配置内容。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t storage_save_config(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t sanitized = *config;
    storage_sanitize_config(&sanitized);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, STORAGE_KEY_CONFIG, &sanitized, sizeof(sanitized));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief 使用默认配置覆盖当前持久化配置。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t storage_reset_config(void)
{
    return storage_save_config(&DEFAULT_CONFIG);
}
