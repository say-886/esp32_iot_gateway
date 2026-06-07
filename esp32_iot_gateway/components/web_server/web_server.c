#include "web_server.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "cJSON.h"
#include "device_status.h"
#include "error_code.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_service.h"
#include "ota_service.h"
#include "storage_nvs.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;
static volatile bool s_ota_running;

typedef struct {
    char url[192];
} ota_task_context_t;

static bool constant_time_equal(const char *left, const char *right)
{
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

static esp_err_t require_api_auth(httpd_req_t *req)
{
    app_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "load auth config failed");
        return ESP_FAIL;
    }

    size_t header_len = httpd_req_get_hdr_value_len(req, "Authorization");
    char authorization[96] = {0};
    bool authorized = header_len > 7 && header_len < sizeof(authorization) &&
                      httpd_req_get_hdr_value_str(req,
                                                  "Authorization",
                                                  authorization,
                                                  sizeof(authorization)) == ESP_OK &&
                      strncmp(authorization, "Bearer ", 7) == 0 &&
                      constant_time_equal(authorization + 7, config.api_token);
    if (authorized) {
        return ESP_OK;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"unauthorized\"}");
    return ESP_ERR_INVALID_STATE;
}

static void ota_upgrade_task(void *arg)
{
    ota_task_context_t *context = (ota_task_context_t *)arg;
    esp_err_t err = ota_service_start_http_upgrade(context->url);
    free(context);
    if (err != ESP_OK) {
        device_status_set_error(APP_ERR_OTA_FAILED);
    } else {
        device_status_clear_error(APP_ERR_OTA_FAILED);
    }
    s_ota_running = false;
    vTaskDelete(NULL);
}

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

/**
 * @brief 发送嵌入到固件二进制中的静态文件（HTML/CSS/JS）。
 * 
 * @param req HTTP 请求句柄。
 * @param content_type 文件的 MIME 类型。
 * @param start 文件在内存中的起始地址。
 * @param end 文件在内存中的结束地址。
 * @return esp_err_t ESP_OK 成功。
 */
static esp_err_t send_embedded_file(httpd_req_t *req,
                                    const char *content_type,
                                    const uint8_t *start,
                                    const uint8_t *end)
{
    size_t length = 0;

    if (req == NULL || content_type == NULL || start == NULL || end == NULL || end < start) {
        return ESP_ERR_INVALID_ARG;
    }

    length = (size_t)(end - start);
    if (length > 0 && end[-1] == '\0') {
        length--;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)start, length);
}

/**
 * @brief 首页 HTML 处理函数。
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/html; charset=utf-8", index_html_start, index_html_end);
}

/**
 * @brief CSS 样式表处理函数。
 */
static esp_err_t style_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/css; charset=utf-8", style_css_start, style_css_end);
}

/**
 * @brief JavaScript 脚本处理函数。
 */
static esp_err_t script_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "application/javascript; charset=utf-8", app_js_start, app_js_end);
}

/**
 * @brief 检查 JSON 请求体中是否包含指定的键。
 */
static esp_err_t receive_request_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len <= 0 || req->content_len >= body_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received_total = 0;
    while (received_total < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + received_total,
                                      req->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += (size_t)received;
    }
    body[received_total] = '\0';
    return ESP_OK;
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

static bool json_copy_optional_string(const cJSON *root, const char *key, char *out, size_t out_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) >= out_size) {
        return false;
    }
    strcpy(out, item->valuestring);
    return true;
}

static bool json_copy_optional_u16(const cJSON *root, const char *key, uint16_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 1 || item->valuedouble > 65535) {
        return false;
    }
    *out = (uint16_t)item->valueint;
    return true;
}

static bool json_copy_optional_u16_allow_zero(const cJSON *root, const char *key, uint16_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 65535) {
        return false;
    }
    *out = (uint16_t)item->valueint;
    return true;
}

static bool json_copy_optional_u32(const cJSON *root, const char *key, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)item->valuedouble;
    return true;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    device_status_t status;
    device_status_get(&status);

    char response[512];
    int len = snprintf(response, sizeof(response),
                       "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.1f,"
                       "\"led\":%d,\"buzzer\":%d,\"relay\":%d,"
                       "\"wifi\":%d,\"mqtt\":%d,\"uptime\":%lu,"
                       "\"error_code\":%lu,\"error_flags\":%lu,\"firmware\":\"%s\",\"state\":\"%s\"}",
                       status.temperature,
                       status.humidity,
                       status.light,
                       status.led_on ? 1 : 0,
                       status.buzzer_on ? 1 : 0,
                       status.relay_on ? 1 : 0,
                       status.wifi_connected ? 1 : 0,
                       status.mqtt_connected ? 1 : 0,
                       (unsigned long)status.uptime_sec,
                       (unsigned long)status.error_code,
                       (unsigned long)status.error_flags,
                       status.firmware_version,
                       app_state_to_string(status.state));
    if (len < 0 || len >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status response overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t control_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    char body[128] = {0};
    if (receive_request_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }

    cJSON *root = cJSON_Parse(body);
    device_cmd_t cmd = {0};
    bool valid = root != NULL && cJSON_IsObject(root) &&
                 json_get_bool(root, "led", &cmd.led_set, &cmd.led_value) &&
                 json_get_bool(root, "buzzer", &cmd.buzzer_set, &cmd.buzzer_value) &&
                 json_get_bool(root, "relay", &cmd.relay_set, &cmd.relay_value) &&
                 (cmd.led_set || cmd.buzzer_set || cmd.relay_set);
    if (!valid) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "invalid control payload: %s", body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid control payload");
    }

    ESP_LOGI(TAG,
             "control request: body=%s led_set=%d led=%d buzzer_set=%d buzzer=%d relay_set=%d relay=%d",
             body,
             cmd.led_set ? 1 : 0,
             cmd.led_value ? 1 : 0,
             cmd.buzzer_set ? 1 : 0,
             cmd.buzzer_value ? 1 : 0,
             cmd.relay_set ? 1 : 0,
             cmd.relay_value ? 1 : 0);
    device_status_update_control(&cmd);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "load config failed");
    }

    char response[768];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"wifi_ssid\":\"%s\",\"mqtt_host\":\"%s\",\"mqtt_port\":%u,"
                       "\"mqtt_use_tls\":%s,\"mqtt_username\":\"%s\","
                       "\"device_id\":\"%s\",\"sample_period_ms\":%lu,"
                       "\"modbus_enabled\":%s,\"modbus_slave_addr\":%u,"
                       "\"modbus_baud_rate\":%lu,\"modbus_start_register\":%u,"
                       "\"modbus_register_count\":%u,\"modbus_poll_period_ms\":%lu}",
                       config.wifi_ssid,
                       config.mqtt_host,
                       (unsigned int)config.mqtt_port,
                       config.mqtt_use_tls ? "true" : "false",
                       config.mqtt_username,
                       config.device_id,
                       (unsigned long)config.sample_period_ms,
                       config.modbus_enabled ? "true" : "false",
                       (unsigned int)config.modbus_slave_addr,
                       (unsigned long)config.modbus_baud_rate,
                       (unsigned int)config.modbus_start_register,
                       (unsigned int)config.modbus_register_count,
                       (unsigned long)config.modbus_poll_period_ms);
    if (len < 0 || len >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config response overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    char body[1024] = {0};
    if (receive_request_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }

    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "load config failed");
    }

    cJSON *root = cJSON_Parse(body);
    bool mqtt_use_tls_present = false;
    bool modbus_enabled_present = false;
    bool valid = root != NULL && cJSON_IsObject(root) &&
                 json_copy_optional_string(root, "wifi_ssid", config.wifi_ssid, sizeof(config.wifi_ssid)) &&
                 json_copy_optional_string(root, "wifi_password", config.wifi_password, sizeof(config.wifi_password)) &&
                 json_copy_optional_string(root, "mqtt_host", config.mqtt_host, sizeof(config.mqtt_host)) &&
                 json_copy_optional_u16(root, "mqtt_port", &config.mqtt_port) &&
                 json_get_bool(root, "mqtt_use_tls", &mqtt_use_tls_present, &config.mqtt_use_tls) &&
                 json_copy_optional_string(root, "mqtt_username", config.mqtt_username, sizeof(config.mqtt_username)) &&
                 json_copy_optional_string(root, "mqtt_password", config.mqtt_password, sizeof(config.mqtt_password)) &&
                 json_copy_optional_string(root, "device_id", config.device_id, sizeof(config.device_id)) &&
                 json_copy_optional_string(root, "api_token", config.api_token, sizeof(config.api_token)) &&
                 json_copy_optional_u32(root, "sample_period_ms", &config.sample_period_ms) &&
                 json_get_bool(root, "modbus_enabled", &modbus_enabled_present, &config.modbus_enabled) &&
                 json_copy_optional_u16_allow_zero(root, "modbus_start_register", &config.modbus_start_register) &&
                 json_copy_optional_u16(root, "modbus_register_count", &config.modbus_register_count) &&
                 json_copy_optional_u32(root, "modbus_baud_rate", &config.modbus_baud_rate) &&
                 json_copy_optional_u32(root, "modbus_poll_period_ms", &config.modbus_poll_period_ms) &&
                 storage_validate_config(&config) == ESP_OK;
    uint16_t slave_addr = config.modbus_slave_addr;
    if (valid) {
        valid = json_copy_optional_u16(root, "modbus_slave_addr", &slave_addr) && slave_addr <= 247;
        config.modbus_slave_addr = (uint8_t)slave_addr;
    }
    cJSON_Delete(root);
    if (!valid) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid config payload");
    }

    err = storage_save_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save config failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

static esp_err_t modbus_status_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    modbus_service_status_t status;
    modbus_service_get_status(&status);
    char response[512];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"enabled\":%s,\"online\":%s,\"slave_addr\":%u,"
                       "\"start_register\":%u,\"register_count\":%u,"
                       "\"success_count\":%lu,\"error_count\":%lu,"
                       "\"consecutive_failures\":%lu,\"last_error\":%ld,\"registers\":[",
                       status.enabled ? "true" : "false",
                       status.online ? "true" : "false",
                       status.slave_addr,
                       status.start_register,
                       status.register_count,
                       (unsigned long)status.success_count,
                       (unsigned long)status.error_count,
                       (unsigned long)status.consecutive_failures,
                       (long)status.last_error);
    if (len < 0 || len >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "modbus response overflow");
    }

    for (uint16_t i = 0; i < status.register_count; ++i) {
        int appended = snprintf(response + len,
                                sizeof(response) - (size_t)len,
                                "%s%u",
                                i == 0 ? "" : ",",
                                status.registers[i]);
        if (appended < 0 || appended >= (int)(sizeof(response) - (size_t)len)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "modbus response overflow");
        }
        len += appended;
    }
    if (len + 2 >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "modbus response overflow");
    }
    response[len++] = ']';
    response[len++] = '}';
    response[len] = '\0';

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (require_api_auth(req) != ESP_OK) {
        return ESP_OK;
    }

    char body[256] = {0};
    char url[192] = {0};
    if (receive_request_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }

    cJSON *root = cJSON_Parse(body);
    const cJSON *url_item = root != NULL ? cJSON_GetObjectItemCaseSensitive(root, "url") : NULL;
    bool valid = cJSON_IsString(url_item) && url_item->valuestring != NULL &&
                 strlen(url_item->valuestring) < sizeof(url);
    if (valid) {
        strcpy(url, url_item->valuestring);
    }
    cJSON_Delete(root);
    if (!valid) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing url");
    }
    if (strncmp(url, "https://", 8) != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "https url required");
    }
    if (s_ota_running) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "ota already running");
    }

    ota_task_context_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota allocation failed");
    }
    strcpy(context->url, url);
    s_ota_running = true;
    if (xTaskCreate(ota_upgrade_task, "ota_upgrade", 8192, context, 5, NULL) != pdPASS) {
        s_ota_running = false;
        free(context);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota task create failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"ota_started\":true}");
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 11;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t index_html_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t style_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t script_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = script_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = control_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t reboot_uri = {
        .uri = "/api/reboot",
        .method = HTTP_POST,
        .handler = reboot_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t ota_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t modbus_uri = {
        .uri = "/api/modbus",
        .method = HTTP_GET,
        .handler = modbus_status_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &index_html_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &style_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &script_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &control_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &config_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &config_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &reboot_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &ota_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &modbus_uri));
    ESP_LOGI(TAG, "HTTP API started");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server != NULL) {
        esp_err_t err = httpd_stop(s_server);
        s_server = NULL;
        return err;
    }
    return ESP_OK;
}
