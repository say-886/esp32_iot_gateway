#ifndef APP_STATE_H
#define APP_STATE_H

typedef enum {
    DEVICE_STATE_INIT = 0,
    DEVICE_STATE_WIFI_CONNECTING, // WiFi 连接中状态
    DEVICE_STATE_MQTT_CONNECTING, // MQTT 连接中状态
    DEVICE_STATE_ONLINE,     // 在线状态
    DEVICE_STATE_ERROR,      // 错误状态
    DEVICE_STATE_RECOVERY    // 恢复状态
} device_state_t;

const char *app_state_to_string(device_state_t state);

#endif
