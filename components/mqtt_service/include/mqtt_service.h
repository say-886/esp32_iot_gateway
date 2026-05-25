#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "device_status.h"
#include "esp_err.h"

esp_err_t mqtt_service_start(void);
esp_err_t mqtt_service_stop(void);
esp_err_t mqtt_service_publish_status(const device_status_t *status);
esp_err_t mqtt_service_publish_sensor(const device_status_t *status);
esp_err_t mqtt_service_publish_heartbeat(const device_status_t *status);
esp_err_t mqtt_service_publish_error(const device_status_t *status);

#endif
