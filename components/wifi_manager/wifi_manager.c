#include "wifi_manager.h"

#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "device_status.h"
#include "error_code.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_service.h"
#include "storage_nvs.h"

static const char *TAG = "wifi_manager";
static volatile bool s_wifi_connected;
static bool s_wifi_initialized;
static bool s_sntp_initialized;
static esp_timer_handle_t s_reconnect_timer;
static uint32_t s_reconnect_attempt;
static TaskHandle_t s_network_gate_task;

#define WIFI_RECONNECT_BASE_MS 1000U        // 基础重连时间，1秒
#define WIFI_RECONNECT_MAX_MS 30000U        // 最大重连时间，30秒
#define WIFI_ERROR_THRESHOLD 5U           // 错误阈值，5次重连失败后，重连时间翻倍
#define NETWORK_GATE_POLL_MS 500U
#define SNTP_WAIT_TIMEOUT_MS 15000U
#define VALID_UNIX_TIME 1700000000LL

static bool system_time_is_valid(void)
{
    return time(NULL) >= VALID_UNIX_TIME;
}

static esp_err_t ensure_sntp_started(void)
{
    if (s_sntp_initialized) {
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        s_sntp_initialized = true;
        ESP_LOGI(TAG, "SNTP started; waiting for trusted system time");
    }
    return err;
}

/**
 * @brief 将 MQTT 启动门控在 Wi-Fi 和可信时间之后。
 *
 * 该任务独立于系统事件循环运行，避免在 IP 事件回调中阻塞。SNTP 保持后台周期
 * 校时；断网后 MQTT 由事件回调停止，重连并恢复有效时间后再重新启动。
 */
static void network_gate_task(void *arg)
{
    (void)arg;
    while (true) {
        if (!s_wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(NETWORK_GATE_POLL_MS));
            continue;
        }

        esp_err_t err = ensure_sntp_started();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(SNTP_WAIT_TIMEOUT_MS));
            continue;
        }

        while (s_wifi_connected && !system_time_is_valid()) {
            err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_WAIT_TIMEOUT_MS));
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "SNTP wait failed: %s", esp_err_to_name(err));
            }
            if (!system_time_is_valid()) {
                ESP_LOGW(TAG, "trusted time unavailable; MQTT/TLS remains gated");
            }
        }
        if (!s_wifi_connected) {
            continue;
        }

        ESP_LOGI(TAG, "system time synchronized; starting MQTT/TLS services");
        err = mqtt_service_start();
        if (err != ESP_OK) {
            device_status_set_error(APP_ERR_MQTT_CONNECT_FAILED);
            ESP_LOGW(TAG, "mqtt start rejected or failed: %s", esp_err_to_name(err));
        }

        while (s_wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(NETWORK_GATE_POLL_MS));
        }
    }
}

/**
 * @brief 重连定时器回调函数。
 *
 * 该函数会在重连定时器到期时被调用，尝试重新连接 WiFi。
 * 如果重连失败，会根据当前重连尝试次数，计算出下一个重连时间。
 * 如果重连时间超过最大重连时间，会设置为最大重连时间。
 */
static void wifi_reconnect_timer_callback(void *arg)
{
    (void)arg;
    esp_err_t err = esp_wifi_connect();     // 尝试重新连接 WiFi
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi reconnect request failed: %s", esp_err_to_name(err));
    }
}

/**
 * @brief 计划一次重连尝试。
 *
 * 该函数会根据当前重连尝试次数，计算出下一个重连时间。
 * 如果重连时间超过最大重连时间，会设置为最大重连时间。
 */
static void wifi_schedule_reconnect(void)
{
    uint32_t shift = s_reconnect_attempt < 5U ? s_reconnect_attempt : 5U;
    uint32_t delay_ms = WIFI_RECONNECT_BASE_MS << shift;
    if (delay_ms > WIFI_RECONNECT_MAX_MS) {
        delay_ms = WIFI_RECONNECT_MAX_MS;
    }
    s_reconnect_attempt++;
    esp_timer_stop(s_reconnect_timer);
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000U);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "schedule reconnect failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "wifi reconnect scheduled in %lu ms, attempt=%lu",
                 (unsigned long)delay_ms, (unsigned long)s_reconnect_attempt);
    }
}

/**
 * @brief 检查 WiFi 配置是否为初始占位符。
 *
 * @param config 指向待检查配置的指针。
 * @return true 是占位符。
 * @return false 已配置真实凭据。
 */
static bool wifi_config_is_placeholder(const app_config_t *config)
{
    return config == NULL ||
           config->wifi_ssid[0] == '\0' ||
           strcmp(config->wifi_ssid, "YOUR_WIFI_SSID") == 0 ||
           strcmp(config->wifi_password, "YOUR_WIFI_PASSWORD") == 0;
}

/**
 * @brief 处理 ESP-IDF 的 Wi-Fi 和 IP 事件。
 *
 * 该回调会同步更新设备共享状态，并在站点断开后触发重连流程。
 *
 * @param arg 用户上下文参数，当前未使用。
 * @param event_base 事件域，用于区分 Wi-Fi/IP 等不同来源。
 * @param event_id 具体事件编号。
 * @param event_data 事件附带的数据，当前未使用。
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "wifi station started");
        /* STA 启动仅表示开始联网流程，尚未真正连上网络。 */
        device_status_update_network(false, false);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;
        s_wifi_connected = false;
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_stop());
        device_status_update_network(false, false);
        if (s_reconnect_attempt >= WIFI_ERROR_THRESHOLD) {
            device_status_set_error(APP_ERR_WIFI_CONNECT_FAILED);
        }
        ESP_LOGW(TAG, "wifi disconnected, reason=%u", disconnected != NULL ? disconnected->reason : 0U);
        wifi_schedule_reconnect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        s_reconnect_attempt = 0;
        esp_timer_stop(s_reconnect_timer);
        device_status_update_network(true, false);
        ESP_LOGI(TAG, "wifi got ip; MQTT waits for SNTP time validation");
    }
}

/**
 * @brief 初始化网络接口、事件循环和 Wi-Fi 驱动状态。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t wifi_manager_init(void)
{
    s_wifi_connected = false;
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t reconnect_timer_args = {
        .callback = wifi_reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    err = esp_timer_create(&reconnect_timer_args, &s_reconnect_timer);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (s_network_gate_task == NULL &&
        xTaskCreate(network_gate_task,
                    "network_gate",
                    4096,
                    NULL,
                    4,
                    &s_network_gate_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_wifi_initialized = true;
    return ESP_OK;
}

/**
 * @brief 读取已保存的 Wi-Fi 配置并启动 Station 模式。
 *
 * @return 成功返回 `ESP_OK`，失败返回对应 ESP-IDF 错误码。
 */
esp_err_t wifi_manager_start(void)
{
    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    if (wifi_config_is_placeholder(&config)) {
        ESP_LOGW(TAG, "WiFi credentials are not configured yet, skip station start");
        device_status_set_error(APP_ERR_WIFI_CONNECT_FAILED);
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    /* 将持久化凭据复制到 ESP-IDF 的 Station 配置结构体中。 */
    strncpy((char *)wifi_config.sta.ssid,
            config.wifi_ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password,
            config.wifi_password,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    return esp_wifi_start();
}

/**
 * @brief 返回当前缓存的 Wi-Fi 连接状态。
 *
 * @return `true` 表示已连接，`false` 表示未连接。
 */
bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}
