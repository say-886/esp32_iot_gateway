#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "device_status.h"
#include "storage_nvs.h"

static const char *TAG = "wifi_manager";
static bool s_wifi_connected;
static bool s_wifi_initialized;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "wifi station started");
        device_status_update_network(false, false);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        device_status_update_network(false, false);
        ESP_LOGW(TAG, "wifi disconnected, retrying");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        device_status_update_network(true, false);
        ESP_LOGI(TAG, "wifi got ip");
    }
}

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
    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {0};
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

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}
