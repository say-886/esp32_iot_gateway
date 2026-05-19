#include "web_server.h"

#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "device_status.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;

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
        return httpd_resp_send_err(req,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "status response overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static bool json_bool_is_true(const char *body, const char *key)
{
    const char *pos = strstr(body, key);
    if (pos == NULL) {
        return false;
    }
    return strstr(pos, ":1") != NULL || strstr(pos, ":true") != NULL;
}

static bool json_has_key(const char *body, const char *key)
{
    return strstr(body, key) != NULL;
}

static esp_err_t control_handler(httpd_req_t *req)
{
    char body[128] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    device_cmd_t cmd = {0};
    cmd.led_set = json_has_key(body, "\"led\"");
    cmd.led_value = json_bool_is_true(body, "\"led\"");
    cmd.buzzer_set = json_has_key(body, "\"buzzer\"");
    cmd.buzzer_value = json_bool_is_true(body, "\"buzzer\"");
    cmd.relay_set = json_has_key(body, "\"relay\"");
    cmd.relay_value = json_bool_is_true(body, "\"relay\"");
    device_status_update_control(&cmd);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
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
    httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = control_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &control_uri));
    ESP_LOGI(TAG, "HTTP fake data API started");
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
