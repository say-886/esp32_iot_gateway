#include "app_state.h"

const char *app_state_to_string(device_state_t state)
{
    switch (state) {
    case DEVICE_STATE_INIT:
        return "INIT";
    case DEVICE_STATE_WIFI_CONNECTING:
        return "WIFI_CONNECTING";
    case DEVICE_STATE_MQTT_CONNECTING:
        return "MQTT_CONNECTING";
    case DEVICE_STATE_ONLINE:
        return "ONLINE";
    case DEVICE_STATE_ERROR:
        return "ERROR";
    case DEVICE_STATE_RECOVERY:
        return "RECOVERY";
    default:
        return "UNKNOWN";
    }
}
