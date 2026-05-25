#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>

#include "device_status.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "storage_nvs.h"

static const char *TAG = "mqtt_service";
static const char *MQTT_TOPIC_STATUS = "esp32/gateway/status";
static const char *MQTT_TOPIC_SENSOR = "esp32/gateway/sensor";
static const char *MQTT_TOPIC_HEARTBEAT = "esp32/gateway/heartbeat";
static const char *MQTT_TOPIC_CMD = "esp32/gateway/cmd";
static const char *MQTT_TOPIC_ERROR = "esp32/gateway/error";

static esp_mqtt_client_handle_t s_client;
static char s_broker_uri[96];

static bool payload_has_key(const char *body, int len, const char *key)
{
    return body != NULL && len > 0 && strstr(body, key) != NULL;
}

static bool payload_parse_bool_value(const char *body, int len, const char *key, bool *out_value)
{
    const char *pos = NULL;

    if (body == NULL || len <= 0 || out_value == NULL) {
        return false;
    }

    pos = strstr(body, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }

    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }

    if (*pos == '1' || strncmp(pos, "true", 4) == 0) {
        *out_value = true;
        return true;
    }
    if (*pos == '0' || strncmp(pos, "false", 5) == 0) {
        *out_value = false;
        return true;
    }

    return false;
}

static void apply_control_payload(const char *body, int len)
{
    char payload[160] = {0};
    if (body == NULL || len <= 0) {
        return;
    }
    if (len >= (int)sizeof(payload)) {
        len = sizeof(payload) - 1;
    }
    memcpy(payload, body, len);
    payload[len] = '\0';

    device_cmd_t cmd = {
        .led_set = payload_has_key(payload, len, "\"led\""),
        .buzzer_set = payload_has_key(payload, len, "\"buzzer\""),
        .relay_set = payload_has_key(payload, len, "\"relay\""),
    };

    if (cmd.led_set && !payload_parse_bool_value(payload, len, "\"led\"", &cmd.led_value)) {
        cmd.led_set = false;
    }
    if (cmd.buzzer_set && !payload_parse_bool_value(payload, len, "\"buzzer\"", &cmd.buzzer_value)) {
        cmd.buzzer_set = false;
    }
    if (cmd.relay_set && !payload_parse_bool_value(payload, len, "\"relay\"", &cmd.relay_value)) {
        cmd.relay_set = false;
    }

    ESP_LOGI(TAG, "MQTT command payload: %s", payload);
    device_status_update_control(&cmd);
}

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
            apply_control_payload(event->data, event->data_len);
            ESP_LOGI(TAG, "MQTT command applied");
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static esp_err_t mqtt_publish(const char *topic, const char *payload, int len)
{
    if (s_client == NULL || topic == NULL || payload == NULL || len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, 1, 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

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
                       "mqtt://%s:%u",
                       config.mqtt_host,
                       (unsigned int)config.mqtt_port);
    if (len < 0 || len >= (int)sizeof(s_broker_uri)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = config.device_id,
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
                       "\"wifi\":%d,\"mqtt\":%d,\"uptime\":%lu,\"error_code\":%lu}",
                       status->temperature,
                       status->humidity,
                       status->light,
                       status->led_on ? 1 : 0,
                       status->buzzer_on ? 1 : 0,
                       status->relay_on ? 1 : 0,
                       status->wifi_connected ? 1 : 0,
                       status->mqtt_connected ? 1 : 0,
                       (unsigned long)status->uptime_sec,
                       (unsigned long)status->error_code);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_STATUS, payload, len);
}

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

esp_err_t mqtt_service_publish_error(const device_status_t *status)
{
    if (s_client == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[192];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"error_code\":%lu,\"uptime\":%lu}",
                       (unsigned long)status->error_code,
                       (unsigned long)status->uptime_sec);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_publish(MQTT_TOPIC_ERROR, payload, len);
}
