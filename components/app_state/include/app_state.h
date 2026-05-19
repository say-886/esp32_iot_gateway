#ifndef APP_STATE_H
#define APP_STATE_H

typedef enum {
    DEVICE_STATE_INIT = 0,
    DEVICE_STATE_WIFI_CONNECTING,
    DEVICE_STATE_MQTT_CONNECTING,
    DEVICE_STATE_ONLINE,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_RECOVERY
} device_state_t;

const char *app_state_to_string(device_state_t state);

#endif
