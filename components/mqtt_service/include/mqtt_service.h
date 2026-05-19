#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "device_status.h"
#include "esp_err.h"

esp_err_t mqtt_service_start(void);
esp_err_t mqtt_service_stop(void);
esp_err_t mqtt_service_publish_status(const device_status_t *status);

#endif
