#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "device_status.h"
#include "edge_compute.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "offline_store.h"
#include "storage_nvs.h"

#define MQTT_TOPIC_ROOT "esp32/gateway"
#define MQTT_KEEPALIVE_SECONDS 30
#define MQTT_OUTBOX_LIMIT_BYTES (16U * 1024U)
#define MQTT_REPLAY_OUTBOX_HIGH_WATER (12U * 1024U)
#define MQTT_RECONNECT_BASE_MS 1000U
#define MQTT_RECONNECT_MAX_MS 30000U
#define MQTT_REPLAY_PERIOD_MS 500U
#define MQTT_RECENT_COMMAND_COUNT 8U
#define MQTT_CAPTURED_OFFLINE_FLAG (1U << 0)
#define MQTT_VALID_UNIX_TIME 1700000000LL

static const char *TAG = "mqtt_service";
static const char *MQTT_TOPIC_STATUS_SUFFIX = "status";
static const char *MQTT_TOPIC_SENSOR_SUFFIX = "sensor";
static const char *MQTT_TOPIC_HEARTBEAT_SUFFIX = "heartbeat";
static const char *MQTT_TOPIC_CMD_SUFFIX = "cmd";
static const char *MQTT_TOPIC_CMD_ACK_SUFFIX = "cmd_ack";
static const char *MQTT_TOPIC_ERROR_SUFFIX = "error";

/*
 * s_state_mutex 保护客户端句柄、连接状态、重连计数和唯一遥测 inflight。
 * MQTT 回调、ESP Timer 回调、业务任务与补传任务都会访问这些字段。
 */
static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_state_mutex;
static TaskHandle_t s_replay_task;
static esp_timer_handle_t s_reconnect_timer;
static bool s_connected;
static bool s_stopping;
static uint32_t s_reconnect_attempt;
static int s_inflight_msg_id = -1;
static uint32_t s_inflight_sequence;
static uint32_t s_boot_id;
static uint32_t s_sequence;
static char s_recent_cmd_ids[MQTT_RECENT_COMMAND_COUNT][64];
static uint32_t s_recent_cmd_next;

static char s_broker_uri[96];
static char s_device_id[32];
static char s_lwt_payload[128];
static char s_topic_status[96];
static char s_topic_sensor[96];
static char s_topic_heartbeat[96];
static char s_topic_cmd[96];
static char s_topic_cmd_ack[96];
static char s_topic_error[96];

/** @brief 获取 MQTT 运行状态互斥锁。 */
static void state_lock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

static void state_unlock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
}

static esp_err_t mqtt_build_topic(char *buffer,
                                  size_t buffer_size,
                                  const char *device_id,
                                  const char *suffix)
{
    if (buffer == NULL || buffer_size == 0 || device_id == NULL || device_id[0] == '\0' ||
        suffix == NULL || suffix[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    int len = snprintf(buffer, buffer_size, "%s/%s/%s", MQTT_TOPIC_ROOT, device_id, suffix);
    return len >= 0 && len < (int)buffer_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t mqtt_prepare_topics(const char *device_id)
{
    if (device_id == NULL || device_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mqtt_build_topic(s_topic_status, sizeof(s_topic_status), device_id,
                                     MQTT_TOPIC_STATUS_SUFFIX);
    if (err == ESP_OK) {
        err = mqtt_build_topic(s_topic_sensor, sizeof(s_topic_sensor), device_id,
                               MQTT_TOPIC_SENSOR_SUFFIX);
    }
    if (err == ESP_OK) {
        err = mqtt_build_topic(s_topic_heartbeat, sizeof(s_topic_heartbeat), device_id,
                               MQTT_TOPIC_HEARTBEAT_SUFFIX);
    }
    if (err == ESP_OK) {
        err = mqtt_build_topic(s_topic_cmd, sizeof(s_topic_cmd), device_id,
                               MQTT_TOPIC_CMD_SUFFIX);
    }
    if (err == ESP_OK) {
        err = mqtt_build_topic(s_topic_cmd_ack, sizeof(s_topic_cmd_ack), device_id,
                               MQTT_TOPIC_CMD_ACK_SUFFIX);
    }
    if (err == ESP_OK) {
        err = mqtt_build_topic(s_topic_error, sizeof(s_topic_error), device_id,
                               MQTT_TOPIC_ERROR_SUFFIX);
    }
    if (err != ESP_OK) {
        return err;
    }

    int copied = snprintf(s_device_id, sizeof(s_device_id), "%s", device_id);
    if (copied < 0 || copied >= (int)sizeof(s_device_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    int lwt_len = snprintf(s_lwt_payload,
                           sizeof(s_lwt_payload),
                           "{\"schema\":1,\"device_id\":\"%s\",\"online\":false}",
                           s_device_id);
    return lwt_len >= 0 && lwt_len < (int)sizeof(s_lwt_payload) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

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

/** @brief 判断命令 ID 是否存在于最近执行环形表中。 */
static bool command_was_executed(const char *cmd_id)
{
    bool found = false;
    state_lock();
    for (uint32_t i = 0; i < MQTT_RECENT_COMMAND_COUNT; ++i) {
        if (s_recent_cmd_ids[i][0] != '\0' && strcmp(s_recent_cmd_ids[i], cmd_id) == 0) {
            found = true;
            break;
        }
    }
    state_unlock();
    return found;
}

/** @brief 记住最近执行的命令 ID，抵御 QoS 1 重复投递。 */
static void remember_executed_command(const char *cmd_id)
{
    state_lock();
    snprintf(s_recent_cmd_ids[s_recent_cmd_next],
             sizeof(s_recent_cmd_ids[s_recent_cmd_next]),
             "%s",
             cmd_id);
    s_recent_cmd_next = (s_recent_cmd_next + 1U) % MQTT_RECENT_COMMAND_COUNT;
    state_unlock();
}

static int mqtt_publish_raw(const char *topic, const char *payload, int len, int qos, int retain)
{
    if (topic == NULL || payload == NULL || len <= 0) {
        return -1;
    }
    state_lock();
    esp_mqtt_client_handle_t client = s_client;
    bool connected = s_connected && !s_stopping;
    int msg_id = connected && client != NULL
                     ? esp_mqtt_client_publish(client, topic, payload, len, qos, retain)
                     : -1;
    state_unlock();
    return msg_id;
}

static void publish_command_ack(const char *cmd_id, const char *result, int code)
{
    device_status_t status;
    device_status_get(&status);
    time_t now = time(NULL);
    uint64_t timestamp_ms = now >= MQTT_VALID_UNIX_TIME ? (uint64_t)now * 1000ULL : 0ULL;
    char payload[384];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"cmd_id\":\"%s\","
                       "\"status\":\"%s\",\"code\":%d,\"timestamp\":%llu,"
                       "\"reported\":{\"led\":%d,\"buzzer\":%d,\"relay\":%d}}",
                       s_device_id,
                       cmd_id != NULL && cmd_id[0] != '\0' ? cmd_id : "unknown",
                       result,
                       code,
                       (unsigned long long)timestamp_ms,
                       status.led_on ? 1 : 0,
                       status.buzzer_on ? 1 : 0,
                       status.relay_on ? 1 : 0);
    if (len > 0 && len < (int)sizeof(payload) &&
        mqtt_publish_raw(s_topic_cmd_ack, payload, len, 1, 0) < 0) {
        ESP_LOGW(TAG, "failed to publish command acknowledgement");
    }
}

static void handle_control_payload(const char *body, int len)
{
    char cmd_id[64] = {0};
    if (body == NULL || len <= 0 || len > 768) {
        publish_command_ack("unknown", "rejected", ESP_ERR_INVALID_SIZE);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        publish_command_ack("unknown", "rejected", ESP_ERR_INVALID_ARG);
        return;
    }

    const cJSON *cmd_id_item = cJSON_GetObjectItemCaseSensitive(root, "cmd_id");
    if (cJSON_IsString(cmd_id_item) && cmd_id_item->valuestring != NULL) {
        snprintf(cmd_id, sizeof(cmd_id), "%s", cmd_id_item->valuestring);
    } else {
        snprintf(cmd_id, sizeof(cmd_id), "legacy-%08lx", (unsigned long)esp_random());
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (type != NULL && (!cJSON_IsString(type) || strcmp(type->valuestring, "control") != 0)) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "rejected", ESP_ERR_NOT_SUPPORTED);
        return;
    }

    const cJSON *expires_at = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    time_t now = time(NULL);
    if (cJSON_IsNumber(expires_at) && now >= MQTT_VALID_UNIX_TIME &&
        expires_at->valuedouble > 0 && (double)now * 1000.0 > expires_at->valuedouble) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "expired", ESP_ERR_TIMEOUT);
        return;
    }

    if (command_was_executed(cmd_id)) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "duplicate", ESP_OK);
        return;
    }

    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    const cJSON *control = cJSON_IsObject(payload) ? payload : root;
    device_cmd_t cmd = {0};
    bool valid = json_get_bool(control, "led", &cmd.led_set, &cmd.led_value) &&
                 json_get_bool(control, "buzzer", &cmd.buzzer_set, &cmd.buzzer_value) &&
                 json_get_bool(control, "relay", &cmd.relay_set, &cmd.relay_value) &&
                 (cmd.led_set || cmd.buzzer_set || cmd.relay_set);
    if (valid) {
        device_status_update_control(&cmd);
        remember_executed_command(cmd_id);
    }
    cJSON_Delete(root);

    if (valid) {
        ESP_LOGI(TAG, "MQTT command applied: %s", cmd_id);
        publish_command_ack(cmd_id, "executed", ESP_OK);
    } else {
        ESP_LOGW(TAG, "invalid MQTT command payload: %s", cmd_id);
        publish_command_ack(cmd_id, "rejected", ESP_ERR_INVALID_ARG);
    }
}

/**
 * @brief 安排带随机抖动的指数退避重连。
 *
 * 退避上限为 30 秒，连接成功后在事件回调中归零，避免 Broker 故障时高频重试。
 */
static void schedule_mqtt_reconnect(void)
{
    if (s_reconnect_timer == NULL || s_stopping) {
        return;
    }
    uint32_t shift = s_reconnect_attempt < 5U ? s_reconnect_attempt : 5U;
    uint32_t delay_ms = MQTT_RECONNECT_BASE_MS << shift;
    if (delay_ms > MQTT_RECONNECT_MAX_MS) {
        delay_ms = MQTT_RECONNECT_MAX_MS;
    }
    uint32_t jitter_range = delay_ms / 5U;
    if (jitter_range > 0U) {
        uint32_t span = jitter_range * 2U + 1U;
        int32_t jitter = (int32_t)(esp_random() % span) - (int32_t)jitter_range;
        delay_ms = (uint32_t)((int32_t)delay_ms + jitter);
    }
    s_reconnect_attempt++;
    esp_timer_stop(s_reconnect_timer);
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
    if (err == ESP_OK) {
        ESP_LOGW(TAG,
                 "MQTT reconnect scheduled in %lu ms, attempt=%lu",
                 (unsigned long)delay_ms,
                 (unsigned long)s_reconnect_attempt);
    }
}

static void mqtt_reconnect_timer_callback(void *arg)
{
    (void)arg;
    state_lock();
    esp_mqtt_client_handle_t client = s_client;
    bool should_reconnect = client != NULL && !s_stopping && !s_connected;
    state_unlock();
    if (!should_reconnect) {
        return;
    }
    esp_err_t err = esp_mqtt_client_reconnect(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT reconnect request failed: %s", esp_err_to_name(err));
        schedule_mqtt_reconnect();
    }
}

static void publish_online_state(void)
{
    char payload[192];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"boot_id\":%lu,"
                       "\"online\":true}",
                       s_device_id,
                       (unsigned long)s_boot_id);
    if (len > 0 && len < (int)sizeof(payload)) {
        mqtt_publish_raw(s_topic_status, payload, len, 1, 1);
    }
}

/**
 * @brief 统一处理 MQTT 连接、断开、QoS 1 PUBACK 与下行命令事件。
 *
 * 只有与当前离线队首对应的 msg_id 收到 MQTT_EVENT_PUBLISHED 后才执行出队，
 * 从而将 Flash 数据生命周期与 Broker 确认绑定。
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
        state_lock();
        s_connected = true;
        s_reconnect_attempt = 0;
        state_unlock();
        esp_timer_stop(s_reconnect_timer);
        device_status_update_network(true, true);
        esp_mqtt_client_subscribe(event->client, s_topic_cmd, 1);
        publish_online_state();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        state_lock();
        s_connected = false;
        s_inflight_msg_id = -1;
        bool stopping = s_stopping;
        state_unlock();
        device_status_update_network(true, false);
        if (!stopping) {
            schedule_mqtt_reconnect();
        }
        break;
    case MQTT_EVENT_PUBLISHED: {
        bool telemetry_acked = false;
        uint32_t sequence = 0;
        state_lock();
        if (s_inflight_msg_id >= 0 && event->msg_id == s_inflight_msg_id) {
            telemetry_acked = true;
            sequence = s_inflight_sequence;
            s_inflight_msg_id = -2;
        }
        state_unlock();
        if (telemetry_acked) {
            esp_err_t pop_err = offline_store_pop();
            if (pop_err != ESP_OK) {
                ESP_LOGW(TAG, "offline queue pop failed after ACK: %s", esp_err_to_name(pop_err));
            } else {
                ESP_LOGD(TAG, "telemetry ACKed: sequence=%lu", (unsigned long)sequence);
            }
            state_lock();
            s_inflight_msg_id = -1;
            state_unlock();
        }
        break;
    }
    case MQTT_EVENT_DATA:
        if (event->topic_len == (int)strlen(s_topic_cmd) &&
            strncmp(event->topic, s_topic_cmd, event->topic_len) == 0) {
            handle_control_payload(event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

/** @brief 将离线队首编码为版本化 JSON 并限制为单条 QoS 1 inflight。 */
static esp_err_t publish_queued_record(const offline_store_record_t *record)
{
    char payload[448];
    bool time_valid = record->timestamp_ms > 0ULL;
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"boot_id\":%lu,"
                       "\"seq\":%lu,\"timestamp\":%llu,\"time_valid\":%s,"
                       "\"uptime_ms\":%lu000,\"replayed\":%s,"
                       "\"data\":{\"temperature\":%.2f,\"humidity\":%.2f,\"light\":%.2f},"
                       "\"edge\":{\"temperature_ema\":%.2f,\"humidity_ema\":%.2f,"
                       "\"light_ema\":%.2f,\"anomaly_flags\":%lu},\"error_code\":%lu}",
                       s_device_id,
                       (unsigned long)record->boot_id,
                       (unsigned long)record->sequence,
                       (unsigned long long)record->timestamp_ms,
                       time_valid ? "true" : "false",
                       (unsigned long)record->uptime_sec,
                       (record->flags & MQTT_CAPTURED_OFFLINE_FLAG) != 0U ? "true" : "false",
                       record->temperature,
                       record->humidity,
                       record->light,
                       record->temperature_ema,
                       record->humidity_ema,
                       record->light_ema,
                       (unsigned long)record->edge_anomaly_flags,
                       (unsigned long)record->error_code);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    state_lock();
    if (!s_connected || s_stopping || s_client == NULL || s_inflight_msg_id != -1 ||
        esp_mqtt_client_get_outbox_size(s_client) >= MQTT_REPLAY_OUTBOX_HIGH_WATER) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_sensor, payload, len, 1, 0);
    if (msg_id >= 0) {
        s_inflight_msg_id = msg_id;
        s_inflight_sequence = record->sequence;
    }
    state_unlock();
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 离线遥测补传任务。
 *
 * 任务按固定短周期检查连接和 outbox 水位，始终从队首顺序发送；损坏记录会计数
 * 后丢弃，避免一条坏数据永久阻塞整个队列。
 */
static void mqtt_replay_task(void *arg)
{
    (void)arg;
    while (true) {
        state_lock();
        bool can_publish = s_connected && !s_stopping && s_client != NULL && s_inflight_msg_id == -1;
        state_unlock();
        if (can_publish) {
            offline_store_record_t record;
            esp_err_t err = offline_store_peek(&record);
            if (err == ESP_ERR_INVALID_CRC) {
                ESP_LOGW(TAG, "dropping corrupted offline record");
                offline_store_pop();
            } else if (err == ESP_OK) {
                err = publish_queued_record(&record);
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "telemetry replay failed: %s", esp_err_to_name(err));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_REPLAY_PERIOD_MS));
    }
}

esp_err_t mqtt_service_start(void)
{
    state_lock();
    if (s_client != NULL) {
        state_unlock();
        return ESP_OK;
    }
    state_unlock();

    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_boot_id == 0U) {
        s_boot_id = esp_random();
    }
    if (s_reconnect_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = mqtt_reconnect_timer_callback,
            .name = "mqtt_reconnect",
        };
        esp_err_t timer_err = esp_timer_create(&timer_args, &s_reconnect_timer);
        if (timer_err != ESP_OK) {
            return timer_err;
        }
    }
    if (s_replay_task == NULL &&
        xTaskCreate(mqtt_replay_task, "mqtt_replay", 4096, NULL, 4, &s_replay_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    err = mqtt_prepare_topics(config.device_id);
    if (err != ESP_OK) {
        return err;
    }

    int uri_len = snprintf(s_broker_uri,
                           sizeof(s_broker_uri),
                           "%s://%s:%u",
                           config.mqtt_use_tls ? "mqtts" : "mqtt",
                           config.mqtt_host,
                           (unsigned int)config.mqtt_port);
    if (uri_len < 0 || uri_len >= (int)sizeof(s_broker_uri)) {
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
        .session.keepalive = MQTT_KEEPALIVE_SECONDS,
        .session.disable_clean_session = true,
        .session.last_will.topic = s_topic_status,
        .session.last_will.msg = s_lwt_payload,
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .network.disable_auto_reconnect = true,
        .outbox.limit = MQTT_OUTBOX_LIMIT_BYTES,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   NULL));

    state_lock();
    s_client = client;
    s_connected = false;
    s_stopping = false;
    s_inflight_msg_id = -1;
    state_unlock();

    err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        state_lock();
        s_client = NULL;
        state_unlock();
        esp_mqtt_client_destroy(client);
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started: %s", s_broker_uri);
    ESP_LOGI(TAG, "MQTT topics: %s | %s | %s | %s | %s | %s",
             s_topic_status,
             s_topic_sensor,
             s_topic_heartbeat,
             s_topic_cmd,
             s_topic_cmd_ack,
             s_topic_error);
    return ESP_OK;
}

esp_err_t mqtt_service_stop(void)
{
    state_lock();
    esp_mqtt_client_handle_t client = s_client;
    if (client == NULL) {
        state_unlock();
        return ESP_OK;
    }
    s_stopping = true;
    s_connected = false;
    s_client = NULL;
    s_inflight_msg_id = -1;
    state_unlock();

    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    esp_err_t err = esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    device_status_update_network(false, false);
    return err;
}

esp_err_t mqtt_service_queue_sensor(const device_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now = time(NULL);
    edge_compute_result_t edge = {0};
    esp_err_t edge_err = edge_compute_process(status->temperature,
                                              status->humidity,
                                              status->light,
                                              &edge);
    if (edge_err != ESP_OK && edge_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "edge compute failed: %s", esp_err_to_name(edge_err));
    }
    state_lock();
    bool connected = s_connected && !s_stopping;
    uint32_t sequence = ++s_sequence;
    state_unlock();

    offline_store_record_t record = {
        .boot_id = s_boot_id,
        .sequence = sequence,
        .uptime_sec = status->uptime_sec,
        .timestamp_ms = now >= MQTT_VALID_UNIX_TIME ? (uint64_t)now * 1000ULL : 0ULL,
        .temperature = status->temperature,
        .humidity = status->humidity,
        .light = status->light,
        .temperature_ema = edge.temperature_ema,
        .humidity_ema = edge.humidity_ema,
        .light_ema = edge.light_ema,
        .edge_anomaly_flags = edge.anomaly_flags,
        .error_code = status->error_code,
        .flags = connected ? 0U : MQTT_CAPTURED_OFFLINE_FLAG,
    };
    esp_err_t err = offline_store_append(&record);
    if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "offline telemetry queue full; newest sample dropped");
    }
    return err;
}

esp_err_t mqtt_service_publish_sensor(const device_status_t *status)
{
    return mqtt_service_queue_sensor(status);
}

esp_err_t mqtt_service_publish_status(const device_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mqtt_service_metrics_t metrics;
    mqtt_service_get_metrics(&metrics);
    char payload[640];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"boot_id\":%lu,"
                       "\"online\":true,\"temperature\":%.2f,\"humidity\":%.2f,\"light\":%.2f,"
                       "\"led\":%d,\"buzzer\":%d,\"relay\":%d,\"wifi\":%d,\"mqtt\":%d,"
                       "\"uptime\":%lu,\"error_code\":%lu,\"error_flags\":%lu,"
                       "\"firmware\":\"%s\",\"state\":\"%s\",\"offline_queue\":%lu,"
                       "\"offline_dropped\":%lu,\"edge_anomaly_flags\":%lu}",
                       s_device_id,
                       (unsigned long)s_boot_id,
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
                       (unsigned long)status->error_flags,
                       status->firmware_version,
                       app_state_to_string(status->state),
                       (unsigned long)metrics.offline_queued,
                       (unsigned long)metrics.offline_dropped,
                       (unsigned long)metrics.edge_anomaly_flags);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mqtt_publish_raw(s_topic_status, payload, len, 1, 1) >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_service_publish_heartbeat(const device_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mqtt_service_metrics_t metrics;
    mqtt_service_get_metrics(&metrics);
    char payload[320];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"boot_id\":%lu,"
                       "\"uptime\":%lu,\"wifi\":%d,\"mqtt\":%d,\"state\":\"%s\","
                       "\"offline_queue\":%lu,\"outbox_bytes\":%lu}",
                       s_device_id,
                       (unsigned long)s_boot_id,
                       (unsigned long)status->uptime_sec,
                       status->wifi_connected ? 1 : 0,
                       status->mqtt_connected ? 1 : 0,
                       app_state_to_string(status->state),
                       (unsigned long)metrics.offline_queued,
                       (unsigned long)metrics.outbox_bytes);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mqtt_publish_raw(s_topic_heartbeat, payload, len, 1, 0) >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_service_publish_error(const device_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char payload[256];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"schema\":1,\"device_id\":\"%s\",\"boot_id\":%lu,"
                       "\"error_code\":%lu,\"error_flags\":%lu,\"uptime\":%lu}",
                       s_device_id,
                       (unsigned long)s_boot_id,
                       (unsigned long)status->error_code,
                       (unsigned long)status->error_flags,
                       (unsigned long)status->uptime_sec);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mqtt_publish_raw(s_topic_error, payload, len, 1, 0) >= 0 ? ESP_OK : ESP_FAIL;
}

void mqtt_service_get_metrics(mqtt_service_metrics_t *metrics)
{
    if (metrics == NULL) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
    offline_store_stats_t stats;
    offline_store_get_stats(&stats);
    metrics->offline_queued = stats.queued;
    metrics->offline_capacity = stats.capacity;
    metrics->offline_dropped = stats.dropped;
    metrics->offline_corrupted = stats.corrupted;
    edge_compute_result_t edge;
    edge_compute_get_snapshot(&edge);
    metrics->edge_temperature_ema = edge.temperature_ema;
    metrics->edge_humidity_ema = edge.humidity_ema;
    metrics->edge_light_ema = edge.light_ema;
    metrics->edge_anomaly_flags = edge.anomaly_flags;
    metrics->edge_sample_count = edge.sample_count;

    state_lock();
    metrics->connected = s_connected && !s_stopping;
    if (s_client != NULL) {
        metrics->outbox_bytes = (uint32_t)esp_mqtt_client_get_outbox_size(s_client);
    }
    state_unlock();
}
