#include "storage_nvs.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("storage_nvs_local.h")
#include "storage_nvs_local.h"
#endif

#define STORAGE_NAMESPACE "gateway"
#define STORAGE_KEY_LEGACY_CONFIG "app_config"
#define STORAGE_KEY_CONFIG "app_cfg_v2"
#define STORAGE_CONFIG_MAGIC 0x47434647U
#define STORAGE_CONFIG_VERSION 2U

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

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t config_size;
    app_config_t config;
} storage_config_record_t;

static const app_config_t DEFAULT_CONFIG = {
    .wifi_ssid = APP_DEFAULT_WIFI_SSID,
    .wifi_password = APP_DEFAULT_WIFI_PASSWORD,
    .mqtt_host = APP_DEFAULT_MQTT_HOST,
    .mqtt_port = APP_DEFAULT_MQTT_PORT,
    .device_id = APP_DEFAULT_DEVICE_ID,
    .sample_period_ms = APP_DEFAULT_SAMPLE_PERIOD_MS,
};

static app_config_t s_cached_config;
static SemaphoreHandle_t s_config_mutex;
static bool s_cache_ready;

static void config_lock(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    }
}

static void config_unlock(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
    }
}

static void sanitize_config(app_config_t *config)
{
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

esp_err_t storage_validate_config(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->mqtt_host[0] == '\0' || config->mqtt_port == 0 ||
        config->device_id[0] == '\0' ||
        config->sample_period_ms < 500 || config->sample_period_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t load_from_nvs(app_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *config = DEFAULT_CONFIG;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    storage_config_record_t record = {0};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, STORAGE_KEY_CONFIG, &record, &size);
    if (err == ESP_OK && size == sizeof(record) &&
        record.magic == STORAGE_CONFIG_MAGIC &&
        record.version == STORAGE_CONFIG_VERSION &&
        record.config_size == sizeof(app_config_t)) {
        *config = record.config;
    } else {
        size = sizeof(*config);
        err = nvs_get_blob(handle, STORAGE_KEY_LEGACY_CONFIG, config, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *config = DEFAULT_CONFIG;
            err = ESP_OK;
        }
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        sanitize_config(config);
    }
    return err;
}

esp_err_t storage_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (s_config_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    app_config_t loaded;
    err = load_from_nvs(&loaded);
    if (err == ESP_OK) {
        config_lock();
        s_cached_config = loaded;
        s_cache_ready = true;
        config_unlock();
    }
    return err;
}

esp_err_t storage_load_config(app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cache_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    config_lock();
    *config = s_cached_config;
    config_unlock();
    return ESP_OK;
}

esp_err_t storage_save_config(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t sanitized = *config;
    sanitize_config(&sanitized);
    esp_err_t err = storage_validate_config(&sanitized);
    if (err != ESP_OK) {
        return err;
    }

    storage_config_record_t record = {
        .magic = STORAGE_CONFIG_MAGIC,
        .version = STORAGE_CONFIG_VERSION,
        .config_size = sizeof(app_config_t),
        .config = sanitized,
    };

    nvs_handle_t handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, STORAGE_KEY_CONFIG, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        config_lock();
        s_cached_config = sanitized;
        s_cache_ready = true;
        config_unlock();
    }
    return err;
}

esp_err_t storage_reset_config(void)
{
    return storage_save_config(&DEFAULT_CONFIG);
}
