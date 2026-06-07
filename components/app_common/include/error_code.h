#ifndef ERROR_CODE_H
#define ERROR_CODE_H

typedef enum {
    APP_ERR_NONE = 0,
    APP_ERR_WIFI_CONNECT_FAILED = 1001, // WiFi 连接失败
    APP_ERR_MQTT_CONNECT_FAILED = 1002, // MQTT 连接失败
    APP_ERR_AHT20_READ_FAILED = 2001,    // AHT20 读取失败
    APP_ERR_BH1750_READ_FAILED = 2002,   // BH1750 读取失败
    APP_ERR_NVS_READ_FAILED = 3001,      // NVS 读取失败
    APP_ERR_NVS_WRITE_FAILED = 3002,     // NVS 写入失败
    APP_ERR_OTA_FAILED = 4001,           // OTA 失败
    APP_ERR_WATCHDOG = 5001            // 看门狗触发
} app_error_code_t;

const char *app_error_code_to_string(app_error_code_t code);

#endif
