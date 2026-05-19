#include "app_config.h"
#include "app_tasks.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "app_tasks";

void app_status_init(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->temperature = 26.5f;
    status->humidity = 60.2f;
    status->light = 380.0f;
    status->error_code = APP_ERR_NONE;
    status->state = DEVICE_STATE_INIT;
    strncpy(status->firmware_version,
            APP_FIRMWARE_VERSION,
            sizeof(status->firmware_version) - 1);
}

void app_create_placeholder_tasks(void)
{
    ESP_LOGI(TAG, "Hardware is not available yet; real FreeRTOS tasks are TODO.");
    ESP_LOGI(TAG, "Planned tasks: sensor, display, web, mqtt, control, button, monitor.");
}
