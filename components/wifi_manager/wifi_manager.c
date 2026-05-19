#include "wifi_manager.h"

static bool s_wifi_connected;

esp_err_t wifi_manager_init(void)
{
    s_wifi_connected = false;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_manager_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}
