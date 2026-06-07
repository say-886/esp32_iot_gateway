#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "device_status.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "storage_nvs.h"

static const char *TAG = "mqtt_service";
static const char *MQTT_TOPIC_STATUS = "esp32/gateway/status";      /**< 状态发布主题 */
static const char *MQTT_TOPIC_SENSOR = "esp32/gateway/sensor";      /**< 传感器数据发布主题 */
static const char *MQTT_TOPIC_HEARTBEAT = "esp32/gateway/heartbeat"; /**< 心跳发布主题 */
static const char *MQTT_TOPIC_CMD = "esp32/gateway/cmd";             /**< 控制命令订阅主题 */
static const char *MQTT_TOPIC_ERROR = "esp32/gateway/error";         /**< 错误发布主题 */

static esp_mqtt_client_handle_t s_client;
static char s_broker_uri[96];

/**
 * @brief 检查 MQTT 消息体中是否包含指定的 JSON 键。
 * 
 * @param body 消息体字符串。
 * @param len 消息体长度。
 * @param key 待查找的键（带双引号，如 "\"led\""）。
 * @return true 包含该键。
 * @return false 不包含该键。
 */
static bool json_get_bool(const cJSON *root, const char *key, bool *present, bool *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    *present = item != NULL;
    if (item == NULL) {
        return true;
    }
    if (cJSON_IsBool(item)) {
        *value = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1)) {
        *value = item->valueint == 1;
        return true;
    }
    return false;
}

static bool apply_control_payload(const char *body, int len)
{
    if (body == NULL || len <= 0 || len > 512) {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    device_cmd_t cmd = {0};
    bool valid = json_get_bool(root, "led", &cmd.led_set, &cmd.led_value) &&
                 json_get_bool(root, "buzzer", &cmd.buzzer_set, &cmd.buzzer_value) &&
                 json_get_bool(root, "relay", &cmd.relay_set, &cmd.relay_value) &&
                 (cmd.led_set || cmd.buzzer_set || cmd.relay_set);

    if (valid) {
        device_status_update_control(&cmd);
    }
    cJSON_Delete(root);
    return valid;
}

/**
 * @brief MQTT 事件处理回调函数。
 * 
 * 处理连接成功、断开连接、收到数据和错误等事件。
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        device_status_update_network(true, true);
        esp_mqtt_client_subscribe(event->client, MQTT_TOPIC_CMD, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        device_status_update_network(true, false);
        break;
    case MQTT_EVENT_DATA:
        if (event->topic_len == (int)strlen(MQTT_TOPIC_CMD) &&
            strncmp(event->topic, MQTT_TOPIC_CMD, event->topic_len) == 0) {
            if (apply_control_payload(event->data, event->data_len)) {
                ESP_LOGI(TAG, "MQTT command applied");
            } else {
                ESP_LOGW(TAG, "invalid MQTT command payload");
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

/**
 * @brief 内部辅助函数：发布 MQTT 消息。
 * 
 * @param topic 发布的主题。
 * @param payload 消息内容。
 * @param len 内容长度。
 * @return esp_err_t ESP_OK 成功。
 */
static esp_err_t mqtt_publish(const char *topic, const char *payload, int len)
{
    if (s_client == NULL || topic == NULL || payload == NULL || len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, 1, 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 启动 MQTT 客户端并连接到 Broker。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_start(void)
{
    if (s_client != NULL) {
        return ESP_OK;
    }

    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    int len = snprintf(s_broker_uri,
                       sizeof(s_broker_uri),
                       "%s://%s:%u",
                       config.mqtt_use_tls ? "mqtts" : "mqtt",
                       config.mqtt_host,
                       (unsigned int)config.mqtt_port);
    if (len < 0 || len >= (int)sizeof(s_broker_uri)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_broker_uri,
        .broker.verification.crt_bundle_attach =
            config.mqtt_use_tls ? esp_crt_bundle_attach : NULL,
        .credentials.username =
            config.mqtt_username[0] != '\0' ? config.mqtt_username : NULL,
        .credentials.client_id = config.device_id,
        .credentials.authentication.password =
            config.mqtt_password[0] != '\0' ? config.mqtt_password : NULL,
    };

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   NULL));
    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started: %s", s_broker_uri);
    return ESP_OK;
}

/**
 * @brief 停止 MQTT 客户端并清理资源。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_stop(void)
{
    if (s_client == NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    device_status_update_network(false, false);
    return err;
}

/**
 * @brief 发布完整的设备状态 JSON 消息。
 * 
 * @param status 状态数据源。
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_publish_status(const device_status_t *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[384];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"temperature\":%.2f,\"humidity\":%.2f,\"light\":%.2f,"
                       "\"led\":%d,\"buzzer\":%d,\"relay\":%d,"
                       "\"wifi\":%d,\"mqtt\":%d,\"uptime\":%lu,\"error_code\":%lu,\"error_flags\":%lu}",
                       status->temperature,
                       status->humidity,
                       status->light,
                       status->led_on ? 1 : 0,
                       status->buzzer_on ? 1 : 0,
                       status->relay_on ? 1 : 0,
                       status->wifi_connected ? 1 : 0,
                       status->mqtt_connected ? 1 : 0,
                       (unsigned long)status->uptime_sec,
                       (unsigned long)status->error_code,
                       (unsigned long)status->error_flags);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_STATUS, payload, len);
}

/**
 * @brief 发布简化的传感器 JSON 消息（温湿度、光照）。
 * 
 * @param status 状态数据源。
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_publish_sensor(const device_status_t *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[192];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"temperature\":%.2f,\"humidity\":%.2f,\"light\":%.2f}",
                       status->temperature,
                       status->humidity,
                       status->light);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_SENSOR, payload, len);
}

/**
 * @brief 发布系统心跳 JSON 消息。
 * 
 * @param status 状态数据源。
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_publish_heartbeat(const device_status_t *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[192];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"uptime\":%lu,\"wifi\":%d,\"mqtt\":%d,\"state\":\"%d\"}",
                       (unsigned long)status->uptime_sec,
                       status->wifi_connected ? 1 : 0,
                       status->mqtt_connected ? 1 : 0,
                       (int)status->state);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_HEARTBEAT, payload, len);
}

/**
 * @brief 发布错误状态 JSON 消息。
 * 
 * @param status 状态数据源。
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t mqtt_service_publish_error(const device_status_t *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[192];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"error_code\":%lu,\"error_flags\":%lu,\"uptime\":%lu}",
                       (unsigned long)status->error_code,
                       (unsigned long)status->error_flags,
                       (unsigned long)status->uptime_sec);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_ERROR, payload, len);
}
