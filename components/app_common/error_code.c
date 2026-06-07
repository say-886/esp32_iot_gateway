#include "error_code.h"

/**
 * @brief 将错误码转换为字符串。
 *
 * @param code 错误码。
 * @return 错误码对应的字符串。
 */
const char *app_error_code_to_string(app_error_code_t code)
{
    switch (code) {
    case APP_ERR_NONE:
        return "NONE";
    case APP_ERR_WIFI_CONNECT_FAILED:
        return "WIFI_CONNECT_FAILED";
    case APP_ERR_MQTT_CONNECT_FAILED:
        return "MQTT_CONNECT_FAILED";
    case APP_ERR_AHT20_READ_FAILED:
        return "AHT20_READ_FAILED";
    case APP_ERR_BH1750_READ_FAILED:
        return "BH1750_READ_FAILED";
    case APP_ERR_NVS_READ_FAILED:
        return "NVS_READ_FAILED";
    case APP_ERR_NVS_WRITE_FAILED:
        return "NVS_WRITE_FAILED";
    case APP_ERR_OTA_FAILED:
        return "OTA_FAILED";
    case APP_ERR_WATCHDOG:
        return "WATCHDOG";
    default:
        return "UNKNOWN";
    }
}
