#ifndef APP_CONFIG_H /* 防止头文件被重复包含。 */
#define APP_CONFIG_H /* 定义头文件保护宏，后续重复 include 时会跳过本文件内容。 */

#include "esp_err.h"
#include "app_state.h"    /* 引入设备状态枚举和状态转字符串接口。 */
#include "app_version.h"  /* 引入固件版本号定义。 */
#include "device_status.h"/* 引入 device_status_t 和 device_cmd_t 等设备状态结构。 */
#include "error_code.h"   /* 引入项目统一错误码定义。 */

#define APP_PROJECT_NAME "esp32_iot_gateway"       /* 项目名称，用于日志、状态和文档标识。 */
#define APP_DEFAULT_SAMPLE_PERIOD_MS 2000U         /* 默认传感器采样周期，单位毫秒。 */
#define APP_DEFAULT_WEB_REFRESH_MS 2000U           /* Web 前端默认刷新周期，单位毫秒。 */
#define APP_DEFAULT_HEARTBEAT_PERIOD_MS 10000U     /* MQTT 心跳默认上报周期，单位毫秒。 */

#define APP_HTTP_API_STATUS "/api/status"          /* HTTP 状态查询接口，返回设备当前状态。 */
#define APP_HTTP_API_CONTROL "/api/control"        /* HTTP 控制接口，用于控制 LED、蜂鸣器、继电器。 */
#define APP_HTTP_API_CONFIG "/api/config"          /* HTTP 配置接口，用于读取或更新运行配置。 */
#define APP_HTTP_API_REBOOT "/api/reboot"          /* HTTP 重启接口，用于触发设备重启。 */
#define APP_HTTP_API_OTA "/api/ota"                /* HTTP OTA 接口，用于通过固件 URL 触发升级。 */
#define APP_HTTP_API_MODBUS "/api/modbus"          /* Modbus RTU 轮询状态接口。 */

#define APP_MQTT_TOPIC_STATUS "esp32/gateway/<device_id>/status"       /* MQTT 设备状态上报主题模板。 */
#define APP_MQTT_TOPIC_SENSOR "esp32/gateway/<device_id>/sensor"       /* MQTT 传感器数据上报主题模板。 */
#define APP_MQTT_TOPIC_HEARTBEAT "esp32/gateway/<device_id>/heartbeat" /* MQTT 心跳上报主题模板。 */
#define APP_MQTT_TOPIC_CMD "esp32/gateway/<device_id>/cmd"             /* MQTT 控制命令订阅主题模板。 */
#define APP_MQTT_TOPIC_CMD_ACK "esp32/gateway/<device_id>/cmd_ack"     /* MQTT 控制命令执行确认主题模板。 */
#define APP_MQTT_TOPIC_ERROR "esp32/gateway/<device_id>/error"         /* MQTT 错误信息上报主题模板。 */

#define APP_ENABLE_I2C_TEST_MODE 0      /* I2C 测试模式开关：1 只测试 I2C，0 启动完整工程。 */
#define APP_ENABLE_NETWORK_SERVICES 1   /* 网络服务开关：1 启动 WiFi/Web/MQTT，0 禁用网络服务。 */

/**
 * @brief 使用默认值初始化应用状态结构体。
 *
 * @param status 输出参数，接收初始化后的状态结构体。
 */
void app_status_init(device_status_t *status); /* 初始化设备状态，通常在 app_main() 启动阶段调用。 */

/**
 * @brief 创建演示骨架中使用的占位 FreeRTOS 任务。
 */
esp_err_t app_create_tasks(void); /* 创建当前项目的 FreeRTOS 业务任务。 */

#endif /* APP_CONFIG_H */ /* 结束头文件保护宏。 */
