#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app_state.h"
#include "app_version.h"
#include "device_status.h"
#include "error_code.h"

#define APP_PROJECT_NAME "esp32_iot_gateway"
#define APP_DEFAULT_SAMPLE_PERIOD_MS 2000U
#define APP_DEFAULT_WEB_REFRESH_MS 2000U
#define APP_DEFAULT_HEARTBEAT_PERIOD_MS 10000U

#define APP_HTTP_API_STATUS "/api/status"
#define APP_HTTP_API_CONTROL "/api/control"
#define APP_HTTP_API_CONFIG "/api/config"
#define APP_HTTP_API_REBOOT "/api/reboot"

#define APP_MQTT_TOPIC_STATUS "esp32/gateway/status"
#define APP_MQTT_TOPIC_SENSOR "esp32/gateway/sensor"
#define APP_MQTT_TOPIC_HEARTBEAT "esp32/gateway/heartbeat"
#define APP_MQTT_TOPIC_CMD "esp32/gateway/cmd"
#define APP_MQTT_TOPIC_ERROR "esp32/gateway/error"

void app_status_init(device_status_t *status);
void app_create_placeholder_tasks(void);

#endif
