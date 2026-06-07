#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "esp_err.h"

esp_err_t ota_service_start_http_upgrade(const char *url);
esp_err_t ota_service_confirm_running_image(void);

#endif
