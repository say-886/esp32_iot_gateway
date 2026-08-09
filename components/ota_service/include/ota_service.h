#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ota_service_start_http_upgrade(const char *url);
esp_err_t ota_service_confirm_running_image(void);
esp_err_t ota_service_schedule_health_confirmation(bool require_time_sync);

#endif
