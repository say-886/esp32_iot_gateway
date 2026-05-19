#include "app_config.h"

#include "board.h"
#include "device_control.h"
#include "esp_log.h"
#include "storage_nvs.h"
#include "watchdog_service.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";
static device_status_t g_device_status;

void app_main(void)
{
    app_status_init(&g_device_status);
    device_status_store_init();

    ESP_LOGI(TAG, "Project: %s", APP_PROJECT_NAME);
    ESP_LOGI(TAG, "Firmware: %s", g_device_status.firmware_version);
    ESP_LOGI(TAG, "Current state: %s",
             app_state_to_string(g_device_status.state));
    ESP_LOGI(TAG, "HTTP APIs: %s, %s, %s, %s",
             APP_HTTP_API_STATUS,
             APP_HTTP_API_CONTROL,
             APP_HTTP_API_CONFIG,
             APP_HTTP_API_REBOOT);
    ESP_LOGI(TAG, "MQTT topics: %s, %s, %s, %s, %s",
             APP_MQTT_TOPIC_STATUS,
             APP_MQTT_TOPIC_SENSOR,
             APP_MQTT_TOPIC_HEARTBEAT,
             APP_MQTT_TOPIC_CMD,
             APP_MQTT_TOPIC_ERROR);

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(device_control_init());
    ESP_ERROR_CHECK(storage_nvs_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(watchdog_service_init());
    app_create_placeholder_tasks();
}
