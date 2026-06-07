#ifndef ERROR_CODE_H
#define ERROR_CODE_H

typedef enum {
    APP_ERR_NONE = 0,
    APP_ERR_WIFI_CONNECT_FAILED = 1001,
    APP_ERR_MQTT_CONNECT_FAILED = 1002,
    APP_ERR_AHT20_READ_FAILED = 2001,
    APP_ERR_BH1750_READ_FAILED = 2002,
    APP_ERR_NVS_READ_FAILED = 3001,
    APP_ERR_NVS_WRITE_FAILED = 3002,
    APP_ERR_OTA_FAILED = 4001,
    APP_ERR_WATCHDOG = 5001,
    APP_ERR_MODBUS_READ_FAILED = 6001
} app_error_code_t;

const char *app_error_code_to_string(app_error_code_t code);

#endif
