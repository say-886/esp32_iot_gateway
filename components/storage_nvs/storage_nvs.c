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
#define STORAGE_KEY_V2_CONFIG "app_cfg_v2"
#define STORAGE_KEY_CONFIG "app_cfg_v3"
#define STORAGE_CONFIG_MAGIC 0x47434647U
#define STORAGE_CONFIG_VERSION 3U

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
#define APP_DEFAULT_MQTT_PORT 8883
#endif
#ifndef APP_DEFAULT_MQTT_USE_TLS
#define APP_DEFAULT_MQTT_USE_TLS true
#endif
#ifndef APP_DEFAULT_MQTT_USERNAME
#define APP_DEFAULT_MQTT_USERNAME ""
#endif
#ifndef APP_DEFAULT_MQTT_PASSWORD
#define APP_DEFAULT_MQTT_PASSWORD ""
#endif
#ifndef APP_DEFAULT_DEVICE_ID
#define APP_DEFAULT_DEVICE_ID "esp32_gateway_001"
#endif
#ifndef APP_DEFAULT_API_TOKEN
#define APP_DEFAULT_API_TOKEN "CHANGE_ME_BEFORE_DEPLOYMENT"
#endif
#ifndef APP_DEFAULT_SAMPLE_PERIOD_MS
#define APP_DEFAULT_SAMPLE_PERIOD_MS 2000
#endif
#ifndef APP_DEFAULT_MODBUS_ENABLED
#define APP_DEFAULT_MODBUS_ENABLED false
#endif
#ifndef APP_DEFAULT_MODBUS_SLAVE_ADDR
#define APP_DEFAULT_MODBUS_SLAVE_ADDR 1
#endif
#ifndef APP_DEFAULT_MODBUS_BAUD_RATE
#define APP_DEFAULT_MODBUS_BAUD_RATE 9600
#endif
#ifndef APP_DEFAULT_MODBUS_START_REGISTER
#define APP_DEFAULT_MODBUS_START_REGISTER 0
#endif
#ifndef APP_DEFAULT_MODBUS_REGISTER_COUNT
#define APP_DEFAULT_MODBUS_REGISTER_COUNT 4
#endif
#ifndef APP_DEFAULT_MODBUS_POLL_PERIOD_MS
#define APP_DEFAULT_MODBUS_POLL_PERIOD_MS 5000
#endif

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char device_id[32];
    uint32_t sample_period_ms;
} app_config_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t config_size;
    app_config_v2_t config;
} storage_config_record_v2_t;

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
    .mqtt_use_tls = APP_DEFAULT_MQTT_USE_TLS,
    .mqtt_username = APP_DEFAULT_MQTT_USERNAME,
    .mqtt_password = APP_DEFAULT_MQTT_PASSWORD,
    .device_id = APP_DEFAULT_DEVICE_ID,
    .api_token = APP_DEFAULT_API_TOKEN,
    .sample_period_ms = APP_DEFAULT_SAMPLE_PERIOD_MS,
    .modbus_enabled = APP_DEFAULT_MODBUS_ENABLED,
    .modbus_slave_addr = APP_DEFAULT_MODBUS_SLAVE_ADDR,
    .modbus_baud_rate = APP_DEFAULT_MODBUS_BAUD_RATE,
    .modbus_start_register = APP_DEFAULT_MODBUS_START_REGISTER,
    .modbus_register_count = APP_DEFAULT_MODBUS_REGISTER_COUNT,
    .modbus_poll_period_ms = APP_DEFAULT_MODBUS_POLL_PERIOD_MS,
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
    config->mqtt_username[sizeof(config->mqtt_username) - 1] = '\0';
    config->mqtt_password[sizeof(config->mqtt_password) - 1] = '\0';
    config->device_id[sizeof(config->device_id) - 1] = '\0';
    config->api_token[sizeof(config->api_token) - 1] = '\0';

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
    if (config->modbus_slave_addr == 0 || config->modbus_slave_addr > 247) {
        config->modbus_slave_addr = DEFAULT_CONFIG.modbus_slave_addr;
    }
    if (config->modbus_baud_rate < 1200 || config->modbus_baud_rate > 1000000) {
        config->modbus_baud_rate = DEFAULT_CONFIG.modbus_baud_rate;
    }
    if (config->modbus_register_count == 0 || config->modbus_register_count > 16) {
        config->modbus_register_count = DEFAULT_CONFIG.modbus_register_count;
    }
    if (config->modbus_poll_period_ms < 500 || config->modbus_poll_period_ms > 60000) {
        config->modbus_poll_period_ms = DEFAULT_CONFIG.modbus_poll_period_ms;
    }
}

static bool config_equal(const app_config_t *left, const app_config_t *right)
{
    return memcmp(left->wifi_ssid, right->wifi_ssid, sizeof(left->wifi_ssid)) == 0 &&
           memcmp(left->wifi_password, right->wifi_password, sizeof(left->wifi_password)) == 0 &&
           memcmp(left->mqtt_host, right->mqtt_host, sizeof(left->mqtt_host)) == 0 &&
           left->mqtt_port == right->mqtt_port &&
           left->mqtt_use_tls == right->mqtt_use_tls &&
           memcmp(left->mqtt_username, right->mqtt_username, sizeof(left->mqtt_username)) == 0 &&
           memcmp(left->mqtt_password, right->mqtt_password, sizeof(left->mqtt_password)) == 0 &&
           memcmp(left->device_id, right->device_id, sizeof(left->device_id)) == 0 &&
           memcmp(left->api_token, right->api_token, sizeof(left->api_token)) == 0 &&
           left->sample_period_ms == right->sample_period_ms &&
           left->modbus_enabled == right->modbus_enabled &&
           left->modbus_slave_addr == right->modbus_slave_addr &&
           left->modbus_baud_rate == right->modbus_baud_rate &&
           left->modbus_start_register == right->modbus_start_register &&
           left->modbus_register_count == right->modbus_register_count &&
           left->modbus_poll_period_ms == right->modbus_poll_period_ms;
}

esp_err_t storage_validate_config(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->mqtt_host[0] == '\0' || config->mqtt_port == 0 ||
        config->device_id[0] == '\0' ||
        config->api_token[0] == '\0' ||
        config->sample_period_ms < 500 || config->sample_period_ms > 60000 ||
        config->modbus_slave_addr == 0 || config->modbus_slave_addr > 247 ||
        config->modbus_baud_rate < 1200 || config->modbus_baud_rate > 1000000 ||
        config->modbus_register_count == 0 || config->modbus_register_count > 16 ||
        config->modbus_poll_period_ms < 500 || config->modbus_poll_period_ms > 60000) {
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
        storage_config_record_v2_t record_v2 = {0};
        size = sizeof(record_v2);
        err = nvs_get_blob(handle, STORAGE_KEY_V2_CONFIG, &record_v2, &size);
        if (err == ESP_OK && size == sizeof(record_v2) &&
            record_v2.magic == STORAGE_CONFIG_MAGIC &&
            record_v2.version == 2U &&
            record_v2.config_size == sizeof(app_config_v2_t)) {
            *config = DEFAULT_CONFIG;
            memcpy(config->wifi_ssid, record_v2.config.wifi_ssid, sizeof(record_v2.config.wifi_ssid));
            memcpy(config->wifi_password, record_v2.config.wifi_password, sizeof(record_v2.config.wifi_password));
            memcpy(config->mqtt_host, record_v2.config.mqtt_host, sizeof(record_v2.config.mqtt_host));
            config->mqtt_port = record_v2.config.mqtt_port;
            memcpy(config->device_id, record_v2.config.device_id, sizeof(record_v2.config.device_id));
            config->sample_period_ms = record_v2.config.sample_period_ms;
        } else {
            app_config_v2_t legacy = {0};
            size = sizeof(legacy);
            err = nvs_get_blob(handle, STORAGE_KEY_LEGACY_CONFIG, &legacy, &size);
            if (err == ESP_OK && size == sizeof(legacy)) {
                *config = DEFAULT_CONFIG;
                memcpy(config->wifi_ssid, legacy.wifi_ssid, sizeof(legacy.wifi_ssid));
                memcpy(config->wifi_password, legacy.wifi_password, sizeof(legacy.wifi_password));
                memcpy(config->mqtt_host, legacy.mqtt_host, sizeof(legacy.mqtt_host));
                config->mqtt_port = legacy.mqtt_port;
                memcpy(config->device_id, legacy.device_id, sizeof(legacy.device_id));
                config->sample_period_ms = legacy.sample_period_ms;
            } else if (err == ESP_ERR_NVS_NOT_FOUND) {
                *config = DEFAULT_CONFIG;
                err = ESP_OK;
            }
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
/**
 * @brief 启动 MQTT 客户端并连接到服务器。
 *
 * 从 NVS 加载 MQTT 连接配置，初始化并启动 MQTT 客户端。
 * 成功后会更新全局状态以反映 MQTT 连接状态。
 *
 * @return esp_err_t ESP_OK 成功。
 */
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

    /* 有效配置未变化时跳过写入，避免消耗 NVS 条目和页擦除次数。 */
    bool unchanged = false;
    config_lock();
    if (s_cache_ready) {
        unchanged = config_equal(&s_cached_config, &sanitized);
    }
    config_unlock();
    if (unchanged) {
        return ESP_OK;
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
