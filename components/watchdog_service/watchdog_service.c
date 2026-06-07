#include "watchdog_service.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "watchdog_service";

esp_err_t watchdog_service_init(void)
{
    esp_task_wdt_config_t config = {
        .timeout_ms = 10000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };

    esp_err_t err = esp_task_wdt_init(&config);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "task watchdog already initialized");
        return ESP_OK;
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "task watchdog initialized");
    }
    return err;
}

esp_err_t watchdog_service_register_current_task(void)
{
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

esp_err_t watchdog_service_feed(void)
{
    return esp_task_wdt_reset();
}
