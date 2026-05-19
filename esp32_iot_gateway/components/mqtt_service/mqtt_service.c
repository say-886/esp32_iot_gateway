#include "mqtt_service.h"

esp_err_t mqtt_service_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mqtt_service_stop(void)
{
    return ESP_OK;
}

esp_err_t mqtt_service_publish_status(const device_status_t *status)
{
    (void)status;
    return ESP_ERR_NOT_SUPPORTED;
}
