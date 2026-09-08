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
#define MQTT_RAM_QUEUE_CAPACITY 8U
#define MQTT_CAPTURED_OFFLINE_FLAG (1U << 0)
#define MQTT_VALID_UNIX_TIME 1700000000LL
#define MQTT_MIN_API_TOKEN_LENGTH 16U

#ifndef APP_ALLOW_INSECURE_DEMO_MQTT
#define APP_ALLOW_INSECURE_DEMO_MQTT 0
#endif

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
/** @brief 当前 MQTT 客户端句柄。*/
static esp_mqtt_client_handle_t s_client;
/** @brief 保护本模块共享状态的互斥锁。*/
static SemaphoreHandle_t s_state_mutex;
/** @brief 后台补传任务句柄。*/
static TaskHandle_t s_replay_task;
/** @brief MQTT 重连定时器句柄。*/
static esp_timer_handle_t s_reconnect_timer;
/** @brief 当前是否已连接到 broker。*/
static bool s_connected;
/** @brief 是否正在停止服务。*/
static bool s_stopping;
/** @brief 是否因为配置不安全而拒绝启动。*/
static bool s_config_rejected;
/** @brief 当前重连尝试次数。*/
static uint32_t s_reconnect_attempt;
/** @brief 当前正在等待 PUBACK 的消息 ID。*/
static int s_inflight_msg_id = -1;
/** @brief 当前 inflight 消息对应的遥测序号。*/
static uint32_t s_inflight_sequence;
/** @brief 当前 inflight 消息是否来自 Flash 队列。*/
static bool s_inflight_from_flash;
/** @brief RAM 中的待发遥测环形队列。*/
static offline_store_record_t s_ram_queue[MQTT_RAM_QUEUE_CAPACITY];
/** @brief RAM 队列头部下标。*/
static uint32_t s_ram_queue_head;
/** @brief RAM 队列当前记录数。*/
static uint32_t s_ram_queue_count;
/** @brief 本次启动随机生成的 boot_id。*/
static uint32_t s_boot_id;
/** @brief 递增的遥测序号。*/
static uint32_t s_sequence;
/** @brief 最近执行过的命令 ID 环形表。*/
static char s_recent_cmd_ids[MQTT_RECENT_COMMAND_COUNT][64];
/** @brief 最近命令表写入位置。*/
static uint32_t s_recent_cmd_next;

/** @brief 当前 broker URI。*/
static char s_broker_uri[96];
/** @brief 当前设备 ID。*/
static char s_device_id[32];
/** @brief MQTT 遗嘱在线状态的离线载荷。*/
static char s_lwt_payload[128];
/** @brief 状态主题。*/
static char s_topic_status[96];
/** @brief 传感器主题。*/
static char s_topic_sensor[96];
/** @brief 心跳主题。*/
static char s_topic_heartbeat[96];
/** @brief 控制命令主题。*/
static char s_topic_cmd[96];
/** @brief 控制命令确认主题。*/
static char s_topic_cmd_ack[96];
/** @brief 错误上报主题。*/
static char s_topic_error[96];
/** @brief 控制命令校验令牌。*/
static char s_command_token[64];

/**
 * @brief 进入 MQTT 模块共享状态临界区。
 */
static void state_lock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

/**
 * @brief 退出 MQTT 模块共享状态临界区。
 */
static void state_unlock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
}

/**
 * @brief 以尽量固定的比较路径判断两个字符串是否相等，用于令牌校验。
 * @param left 左侧字符串。
 * @param right 右侧字符串。
 * @return bool 两个字符串完全相等时返回 true。
 */
static bool constant_time_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    size_t max_len = left_len > right_len ? left_len : right_len;
    unsigned char difference = (unsigned char)(left_len ^ right_len);
    for (size_t i = 0; i < max_len; ++i) {
        unsigned char left_value = i < left_len ? (unsigned char)left[i] : 0U;
        unsigned char right_value = i < right_len ? (unsigned char)right[i] : 0U;
        difference |= left_value ^ right_value;
    }
    return difference == 0U;
}

/**
 * @brief 判断 API token 是否满足最小长度且不是默认占位值。
 * @param token 待检查的 API token。
 * @return bool 满足安全要求时返回 true。
 */
static bool api_token_is_secure(const char *token)
{
    return token != NULL && strlen(token) >= MQTT_MIN_API_TOKEN_LENGTH &&
           strcmp(token, "CHANGE_ME_BEFORE_DEPLOYMENT") != 0;
}

/**
 * @brief 判断 broker 是否属于公开演示地址。
 * @param host MQTT broker 主机名。
 * @return bool 命中公开演示地址时返回 true。
 */
static bool mqtt_host_is_public_demo(const char *host)
{
    return host != NULL &&
           (strcmp(host, "broker.emqx.io") == 0 ||
            strcmp(host, "test.mosquitto.org") == 0 ||
            strcmp(host, "broker.hivemq.com") == 0);
}

/**
 * @brief 检查从NVS加载的MQTT配置是否满足当前工程要求的安全约束。
 * @param config 已加载的应用配置。
 * @return esp_err_t 配置合法返回 ESP_OK，否则返回错误码。
 */
static esp_err_t validate_secure_mqtt_config(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#if APP_ALLOW_INSECURE_DEMO_MQTT
    ESP_LOGW(TAG, "insecure demo MQTT mode is explicitly enabled");
    return ESP_OK;
#else
    if (!config->mqtt_use_tls) {
        ESP_LOGE(TAG, "MQTT rejected: TLS is required");
        return ESP_ERR_INVALID_STATE;
    }
    if (mqtt_host_is_public_demo(config->mqtt_host)) {
        ESP_LOGE(TAG, "MQTT rejected: public demo broker is not allowed in secure mode");
        return ESP_ERR_INVALID_STATE;
    }
    if (config->mqtt_username[0] == '\0' || config->mqtt_password[0] == '\0') {
        ESP_LOGE(TAG, "MQTT rejected: broker username and password are required");
        return ESP_ERR_INVALID_STATE;
    }
    if (!api_token_is_secure(config->api_token)) {
        ESP_LOGE(TAG, "MQTT rejected: API token is default or shorter than %u characters",
                 (unsigned int)MQTT_MIN_API_TOKEN_LENGTH);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#endif
}

/**
 * @brief 按环形队列规则计算 RAM 待发队列的物理下标。
 * @param offset 相对队首的偏移量。
 * @return uint32_t 对应的环形队列下标。
 */
static uint32_t ram_queue_index(uint32_t offset)
{
    return (s_ram_queue_head + offset) % MQTT_RAM_QUEUE_CAPACITY;
}

/**
 * @brief 将 RAM 中尚未持久化的遥测记录按原顺序刷入 Flash 队列。
 *
 * 调用者需持有状态锁。
 */
static void persist_ram_queue_locked(void)
{
    for (uint32_t i = 0; i < s_ram_queue_count; ++i) {
        offline_store_record_t record = s_ram_queue[ram_queue_index(i)];
        record.flags |= MQTT_CAPTURED_OFFLINE_FLAG;
        esp_err_t err = offline_store_append(&record);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "RAM telemetry persist failed: sequence=%lu error=%s",
                     (unsigned long)record.sequence,
                     esp_err_to_name(err));
        }
    }
    s_ram_queue_head = 0U;
    s_ram_queue_count = 0U;
}

/**
 * @brief 拼接单个 MQTT topic 字符串，失败时返回长度错误。
 * @param buffer 输出缓冲区。
 * @param buffer_size 输出缓冲区大小。
 * @param device_id 设备 ID。
 * @param suffix topic 后缀。
 * @return esp_err_t 成功返回 ESP_OK，失败返回参数或长度错误。
 */
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

/**
 * @brief 根据设备 ID 生成本模块使用的全部 topic 和遗嘱载荷。
 * @param device_id 设备 ID。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
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

/**
 * @brief 从 JSON 对象中读取布尔值，兼容 true/false 和 0/1 两种写法。
 * @param root JSON 根对象。
 * @param key 字段名。
 * @param present 是否找到该字段。
 * @param value 输出的布尔值。
 * @return bool 解析成功返回 true。
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

/**
 * @brief 检查命令 ID 是否已经执行过，用于抑制 QoS1 重复投递。
 * @param cmd_id 命令 ID。
 * @return bool 已执行过返回 true。
 */
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

/**
 * @brief 记录最近执行过的命令 ID。
 * @param cmd_id 命令 ID。
 */
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

/**
 * @brief 在连接可用时直接发布原始 MQTT 消息。
 * @param topic 主题。
 * @param payload 载荷。
 * @param len 载荷长度。
 * @param qos QoS 等级。
 * @param retain 是否保留。
 * @return int 发布成功返回 msg_id，失败返回 -1。
 */
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

/**
 * @brief 发布控制命令执行结果确认包。
 * @param cmd_id 命令 ID。
 * @param result 执行结果字符串。
 * @param code 对应错误码或状态码。
 */
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

/**
 * @brief 解析并执行来自 MQTT 控制主题的命令载荷。
 * @param body MQTT 消息正文。
 * @param len 消息长度。
 */
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
    if (cJSON_IsString(cmd_id_item) && cmd_id_item->valuestring != NULL &&
        cmd_id_item->valuestring[0] != '\0') {
        snprintf(cmd_id, sizeof(cmd_id), "%s", cmd_id_item->valuestring);
    } else {
        cJSON_Delete(root);
        publish_command_ack("unknown", "rejected", ESP_ERR_INVALID_ARG);
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (type != NULL && (!cJSON_IsString(type) || strcmp(type->valuestring, "control") != 0)) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "rejected", ESP_ERR_NOT_SUPPORTED);
        return;
    }

    const cJSON *auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
    if (!cJSON_IsString(auth) || auth->valuestring == NULL ||
        !constant_time_equal(auth->valuestring, s_command_token)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "MQTT command authentication failed: %s", cmd_id);
        publish_command_ack(cmd_id, "unauthorized", ESP_ERR_INVALID_STATE);
        return;
    }

    const cJSON *expires_at = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    time_t now = time(NULL);
    if (!cJSON_IsNumber(expires_at) || expires_at->valuedouble <= 0) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "rejected", ESP_ERR_INVALID_ARG);
        return;
    }
    if (now < MQTT_VALID_UNIX_TIME) {
        cJSON_Delete(root);
        publish_command_ack(cmd_id, "time_unavailable", ESP_ERR_INVALID_STATE);
        return;
    }
    if ((double)now * 1000.0 > expires_at->valuedouble) {
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
/**
 * @brief 按指数退避并叠加抖动，安排下一次 MQTT 重连。
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

/**
 * @brief ESP 定时器回调：到点后发起 MQTT 重连请求。
 * @param arg 定时器上下文参数。
 */
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

/**
 * @brief 发布在线状态遗嘱对应的在线确认消息。
 */
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
/**
 * @brief 统一处理 MQTT 连接事件、下行命令和发布确认。
 * @param handler_args 事件处理器参数。
 * @param base 事件基。
 * @param event_id 事件 ID。
 * @param event_data 事件数据。
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
        persist_ram_queue_locked();
        s_inflight_msg_id = -1;
        s_inflight_from_flash = false;
        bool stopping = s_stopping;
        state_unlock();
        device_status_update_network(true, false);
        if (!stopping) {
            schedule_mqtt_reconnect();
        }
        break;
    case MQTT_EVENT_PUBLISHED: {
        bool telemetry_acked = false;
        bool from_flash = false;
        uint32_t sequence = 0;
        state_lock();
        if (s_inflight_msg_id >= 0 && event->msg_id == s_inflight_msg_id) {
            telemetry_acked = true;
            from_flash = s_inflight_from_flash;
            sequence = s_inflight_sequence;
            s_inflight_msg_id = -2;
        }
        state_unlock();
        if (telemetry_acked) {
            if (from_flash) {
                esp_err_t pop_err = offline_store_pop();
                if (pop_err != ESP_OK) {
                    ESP_LOGW(TAG, "offline queue pop failed after ACK: %s", esp_err_to_name(pop_err));
                }
            } else {
                state_lock();
                if (s_ram_queue_count > 0U &&
                    s_ram_queue[s_ram_queue_head].sequence == sequence) {
                    s_ram_queue_head = (s_ram_queue_head + 1U) % MQTT_RAM_QUEUE_CAPACITY;
                    s_ram_queue_count--;
                } else {
                    ESP_LOGW(TAG, "RAM telemetry ACK order mismatch: sequence=%lu",
                             (unsigned long)sequence);
                }
                state_unlock();
            }
            ESP_LOGD(TAG, "telemetry ACKed: sequence=%lu source=%s",
                     (unsigned long)sequence,
                     from_flash ? "flash" : "ram");
            state_lock();
            s_inflight_msg_id = -1;
            s_inflight_from_flash = false;
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

/**
 * @brief 将离线/重放遥测记录编码为 JSON 文本。
 * @param record 待编码记录。
 * @param payload 输出缓冲区。
 * @param payload_size 输出缓冲区大小。
 * @return int 编码后的长度，失败返回 -1。
 */
static int encode_telemetry_payload(const offline_store_record_t *record,
                                    char *payload,
                                    size_t payload_size)
{
    if (record == NULL || payload == NULL || payload_size == 0U) {
        return -1;
    }
    bool time_valid = record->timestamp_ms > 0ULL;
    int len = snprintf(payload,
                       payload_size,
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
    return len >= 0 && len < (int)payload_size ? len : -1;
}

/**
 * @brief 从 Flash 队列重放一条遥测记录。
 * @param record 待发布记录。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
static esp_err_t publish_queued_record(const offline_store_record_t *record)
{
    char payload[448];
    int len = encode_telemetry_payload(record, payload, sizeof(payload));
    if (len < 0) {
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
        s_inflight_from_flash = true;
    }
    state_unlock();
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 在在线且无积压时，直接发布 RAM 队首遥测记录。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
static esp_err_t publish_ram_record(void)
{
    char payload[448];
    state_lock();
    if (!s_connected || s_stopping || s_client == NULL || s_inflight_msg_id != -1 ||
        s_ram_queue_count == 0U ||
        esp_mqtt_client_get_outbox_size(s_client) >= MQTT_REPLAY_OUTBOX_HIGH_WATER) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const offline_store_record_t *record = &s_ram_queue[s_ram_queue_head];
    int len = encode_telemetry_payload(record, payload, sizeof(payload));
    if (len < 0) {
        state_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_sensor, payload, len, 1, 0);
    if (msg_id >= 0) {
        s_inflight_msg_id = msg_id;
        s_inflight_sequence = record->sequence;
        s_inflight_from_flash = false;
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
/**
 * @brief 后台补传任务，负责轮询 Flash/RAM 队列并按序重发遥测。
 * @param arg 任务参数。
 */
static void mqtt_replay_task(void *arg)
{
    (void)arg;
    while (true) {
        state_lock();
        bool can_publish = s_connected && !s_stopping && s_client != NULL && s_inflight_msg_id == -1;
        bool ram_pending = s_ram_queue_count > 0U;
        state_unlock();
        if (can_publish) {
            esp_err_t err = ESP_OK;
            if (ram_pending) {
                err = publish_ram_record();
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "RAM telemetry publish failed: %s", esp_err_to_name(err));
                }
            } else {
                offline_store_record_t record;
                err = offline_store_peek(&record);
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
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_REPLAY_PERIOD_MS));
    }
}

/**
 * @brief 初始化 MQTT 客户端、主题、重连定时器和补传任务。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
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
    err = validate_secure_mqtt_config(&config);
    if (err != ESP_OK) {
        state_lock();
        s_config_rejected = true;
        state_unlock();
        return err;
    }
    state_lock();
    s_config_rejected = false;
    state_unlock();
    err = mqtt_prepare_topics(config.device_id);
    if (err != ESP_OK) {
        return err;
    }
    int token_len = snprintf(s_command_token, sizeof(s_command_token), "%s", config.api_token);
    if (token_len < 0 || token_len >= (int)sizeof(s_command_token)) {
        return ESP_ERR_INVALID_SIZE;
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
    s_inflight_from_flash = false;
    s_ram_queue_head = 0U;
    s_ram_queue_count = 0U;
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

/**
 * @brief 停止 MQTT 客户端并把 RAM 中未发记录落盘。
 * @return esp_err_t 停止流程中的错误码。
 */
esp_err_t mqtt_service_stop(void)
{
    state_lock();
    esp_mqtt_client_handle_t client = s_client;
    if (client == NULL) {
        state_unlock();
        esp_err_t flush_err = offline_store_flush();
        return flush_err == ESP_ERR_INVALID_STATE ? ESP_OK : flush_err;
    }
    s_stopping = true;
    s_connected = false;
    persist_ram_queue_locked();
    s_client = NULL;
    s_inflight_msg_id = -1;
    s_inflight_from_flash = false;
    state_unlock();

    esp_err_t flush_err = offline_store_flush();

    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    esp_err_t err = esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    device_status_update_network(false, false);
    return err != ESP_OK ? err : flush_err;
}

/**
 * @brief 采集一条传感器状态并按在线/离线策略排队。
 * @param status 当前设备状态。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
esp_err_t mqtt_service_queue_sensor(const device_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    state_lock();
    bool config_rejected = s_config_rejected;
    state_unlock();
    if (config_rejected) {
        return ESP_ERR_INVALID_STATE;
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
    if (s_boot_id == 0U) {
        s_boot_id = esp_random();
    }
    uint32_t sequence = ++s_sequence;
    uint32_t boot_id = s_boot_id;
    state_unlock();

    offline_store_record_t record = {
        .boot_id = boot_id,
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
        .flags = 0U,
    };

    offline_store_stats_t stats;
    offline_store_get_stats(&stats);
    state_lock();
    bool connected = s_connected && !s_stopping && s_client != NULL;
    if (connected && stats.queued == 0U && s_ram_queue_count < MQTT_RAM_QUEUE_CAPACITY) {
        s_ram_queue[ram_queue_index(s_ram_queue_count)] = record;
        s_ram_queue_count++;
        state_unlock();
        return ESP_OK;
    }
    if (connected && stats.queued == 0U && s_ram_queue_count >= MQTT_RAM_QUEUE_CAPACITY) {
        ESP_LOGW(TAG, "RAM telemetry queue saturated; spilling ordered batch to Flash");
        persist_ram_queue_locked();
        if (s_inflight_msg_id >= 0) {
            s_inflight_from_flash = true;
        }
        record.flags |= MQTT_CAPTURED_OFFLINE_FLAG;
        esp_err_t spill_err = offline_store_append(&record);
        state_unlock();
        return spill_err;
    }
    if (!connected) {
        record.flags |= MQTT_CAPTURED_OFFLINE_FLAG;
    }
    state_unlock();

    esp_err_t err = offline_store_append(&record);
    if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "offline telemetry queue full; newest sample dropped");
    }
    return err;
}

/**
 * @brief 对外兼容接口：与队列传感器逻辑保持一致。
 * @param status 当前设备状态。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
esp_err_t mqtt_service_publish_sensor(const device_status_t *status)
{
    return mqtt_service_queue_sensor(status);
}

/**
 * @brief 发布设备完整状态快照到 status 主题。
 * @param status 当前设备状态。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
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
                        "\"offline_dropped\":%lu,\"offline_faulted\":%d,"
                        "\"offline_last_error\":%ld,\"edge_anomaly_flags\":%lu}",
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
                        metrics.offline_faulted ? 1 : 0,
                        (long)metrics.offline_last_error,
                        (unsigned long)metrics.edge_anomaly_flags);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mqtt_publish_raw(s_topic_status, payload, len, 1, 1) >= 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 发布轻量心跳消息，携带在线性和队列水位信息。
 * @param status 当前设备状态。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
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

/**
 * @brief 发布错误状态摘要。
 * @param status 当前设备状态。
 * @return esp_err_t 成功返回 ESP_OK，否则返回错误码。
 */
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

/**
 * @brief 汇总 MQTT 连接、离线队列和边缘计算指标快照。
 * @param metrics 输出指标结构体。
 */
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
    metrics->offline_meta_erase_count = stats.meta_erase_count;
    metrics->offline_data_erase_count = stats.data_erase_count;
    metrics->offline_faulted = stats.faulted;
    metrics->offline_last_error = stats.last_error;
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
