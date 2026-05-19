#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define APP_PROJECT_NAME "esp32_iot_gateway"
#define APP_FIRMWARE_VERSION "v0.1.0-prep"
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

typedef enum {
    DEVICE_STATE_INIT = 0,
    DEVICE_STATE_WIFI_CONNECTING,
    DEVICE_STATE_MQTT_CONNECTING,
    DEVICE_STATE_ONLINE,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_RECOVERY
} device_state_t;

typedef enum {
    APP_ERR_NONE = 0,
    APP_ERR_WIFI_CONNECT_FAILED = 1001,
    APP_ERR_MQTT_CONNECT_FAILED = 1002,
    APP_ERR_AHT20_READ_FAILED = 2001,
    APP_ERR_BH1750_READ_FAILED = 2002,
    APP_ERR_NVS_READ_FAILED = 3001,
    APP_ERR_NVS_WRITE_FAILED = 3002,
    APP_ERR_OTA_FAILED = 4001,
    APP_ERR_WATCHDOG = 5001
} app_error_code_t;

typedef struct {
    float temperature;
    float humidity;
    float light;
    bool led_on;
    bool buzzer_on;
    bool relay_on;
    bool wifi_connected;
    bool mqtt_connected;
    uint32_t uptime_sec;
    uint32_t error_code;
    char firmware_version[16];
    device_state_t state;
} device_status_t;

typedef struct {
    bool led_set;
    bool led_value;
    bool buzzer_set;
    bool buzzer_value;
    bool relay_set;
    bool relay_value;
} device_cmd_t;

void app_status_init(device_status_t *status);
const char *app_device_state_to_string(device_state_t state);
void app_create_placeholder_tasks(void);

#endif
