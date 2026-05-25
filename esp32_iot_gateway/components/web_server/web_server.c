#include "web_server.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_state.h"
#include "device_status.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_service.h"
#include "storage_nvs.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

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

static esp_err_t index_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/html; charset=utf-8", index_html_start, index_html_end);
}

static esp_err_t style_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/css; charset=utf-8", style_css_start, style_css_end);
}

static esp_err_t script_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "application/javascript; charset=utf-8", app_js_start, app_js_end);
}

static bool json_has_key(const char *body, const char *key)
{
    return body != NULL && strstr(body, key) != NULL;
}

static bool json_parse_bool_value(const char *body, const char *key, bool *out_value)
{
    const char *pos = NULL;

    if (body == NULL || out_value == NULL) {
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

static bool json_copy_string(const char *body, const char *key, char *out, size_t out_size)
{
    const char *pos = NULL;
    const char *start = NULL;
    const char *end = NULL;
    size_t len = 0;

    if (body == NULL || out == NULL || out_size == 0) {
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

    start = strchr(pos, '"');
    if (start == NULL) {
        return false;
    }
    start++;

    end = strchr(start, '"');
    if (end == NULL || end <= start) {
        return false;
    }

    len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool json_copy_u16(const char *body, const char *key, uint16_t *out)
{
    const char *pos = NULL;
    int value = 0;

    if (body == NULL || out == NULL) {
        return false;
    }

    pos = strstr(body, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL || sscanf(pos + 1, "%d", &value) != 1 || value < 0 || value > 65535) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool json_copy_u32(const char *body, const char *key, uint32_t *out)
{
    const char *pos = NULL;
    unsigned long value = 0;

    if (body == NULL || out == NULL) {
        return false;
    }

    pos = strstr(body, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL || sscanf(pos + 1, "%lu", &value) != 1) {
        return false;
    }

    *out = (uint32_t)value;
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
                       "\"error_code\":%lu,\"firmware\":\"%s\",\"state\":\"%s\"}",
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
    char body[128] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    bool parse_failed = false;
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    device_cmd_t cmd = {
        .led_set = json_has_key(body, "\"led\""),
        .buzzer_set = json_has_key(body, "\"buzzer\""),
        .relay_set = json_has_key(body, "\"relay\""),
    };

    if (cmd.led_set && !json_parse_bool_value(body, "\"led\"", &cmd.led_value)) {
        parse_failed = true;
    }
    if (cmd.buzzer_set && !json_parse_bool_value(body, "\"buzzer\"", &cmd.buzzer_value)) {
        parse_failed = true;
    }
    if (cmd.relay_set && !json_parse_bool_value(body, "\"relay\"", &cmd.relay_value)) {
        parse_failed = true;
    }
    if (parse_failed) {
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

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "load config failed");
    }

    char response[384];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"wifi_ssid\":\"%s\",\"mqtt_host\":\"%s\",\"mqtt_port\":%u,"
                       "\"device_id\":\"%s\",\"sample_period_ms\":%lu}",
                       config.wifi_ssid,
                       config.mqtt_host,
                       (unsigned int)config.mqtt_port,
                       config.device_id,
                       (unsigned long)config.sample_period_ms);
    if (len < 0 || len >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config response overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char body[384] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "load config failed");
    }

    json_copy_string(body, "\"wifi_ssid\"", config.wifi_ssid, sizeof(config.wifi_ssid));
    json_copy_string(body, "\"wifi_password\"", config.wifi_password, sizeof(config.wifi_password));
    json_copy_string(body, "\"mqtt_host\"", config.mqtt_host, sizeof(config.mqtt_host));
    json_copy_u16(body, "\"mqtt_port\"", &config.mqtt_port);
    json_copy_string(body, "\"device_id\"", config.device_id, sizeof(config.device_id));
    json_copy_u32(body, "\"sample_period_ms\"", &config.sample_period_ms);

    err = storage_save_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save config failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    char body[256] = {0};
    char url[192] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    if (!json_copy_string(body, "\"url\"", url, sizeof(url))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing url");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"ota_started\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t err = ota_service_start_http_upgrade(url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA request failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
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
