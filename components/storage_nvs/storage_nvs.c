#include "storage_nvs.h"

esp_err_t storage_nvs_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_load_config(app_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_save_config(const app_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_reset_config(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
