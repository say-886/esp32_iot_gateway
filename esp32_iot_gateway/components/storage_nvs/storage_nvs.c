#include "storage_nvs.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define STORAGE_NAMESPACE "gateway"
#define STORAGE_KEY_CONFIG "app_config"

static const app_config_t DEFAULT_CONFIG = {
    .wifi_ssid = "YOUR_WIFI_SSID",
    .wifi_password = "YOUR_WIFI_PASSWORD",
    .mqtt_host = "broker.emqx.io",
    .mqtt_port = 1883,
    .device_id = "esp32_gateway_001",
    .sample_period_ms = 2000,
};

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

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, STORAGE_KEY_CONFIG, config, sizeof(*config));
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
