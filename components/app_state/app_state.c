#include "app_state.h"
/**
 * @brief 将设备状态枚举转换为字符串表示。
 *
 * @param state 设备状态枚举值。
 * @return const char* 对应的字符串表示。
 */
const char *app_state_to_string(device_state_t state)
{
    switch (state) {
    case DEVICE_STATE_INIT:     // 初始化状态
        return "INIT";
    case DEVICE_STATE_WIFI_CONNECTING: // WiFi 连接中状态
        return "WIFI_CONNECTING";
    case DEVICE_STATE_MQTT_CONNECTING: // MQTT 连接中状态
        return "MQTT_CONNECTING";
    case DEVICE_STATE_ONLINE:     // 在线状态
        return "ONLINE";
    case DEVICE_STATE_ERROR:     // 错误状态
        return "ERROR";
    case DEVICE_STATE_RECOVERY:     // 恢复状态
        return "RECOVERY";
    default:
        return "UNKNOWN";
    }
}
