#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

/**
 * @brief 全局设备状态结构体
 * 
 * 该结构体汇总了传感器数据、执行器状态、网络连接状态以及系统元数据。
 * 它是整个系统的数据中心快照。
 */
typedef struct {
    float temperature;          /**< 摄氏温度 (°C) */
    float humidity;             /**< 相对湿度 (%) */
    float light;                /**< 光照强度 (Lux) */
    bool led_on;                /**< LED 指示灯状态 (true: 开启, false: 关闭) */
    bool buzzer_on;             /**< 蜂鸣器状态 (true: 开启, false: 关闭) */
    bool relay_on;              /**< 继电器状态 (true: 开启, false: 关闭) */
    bool wifi_connected;        /**< Wi-Fi 连接状态 (true: 已连接) */
    bool mqtt_connected;        /**< MQTT 连接状态 (true: 已连接) */
    uint32_t uptime_sec;        /**< 系统运行时间 (秒) */
    uint32_t error_code;        /**< 系统错误码 (0 表示正常) */
    uint32_t error_flags;       /**< 当前活动错误位图 */
    char firmware_version[16];  /**< 固件版本号字符串 */
    device_state_t state;       /**< 系统运行状态机状态 */
} device_status_t;

typedef struct {
    bool led_set;               /**< 是否设置 LED 状态 */
    bool led_value;             /**< LED 状态值 (true: 开启, false: 关闭) */
    bool buzzer_set;            /**< 是否设置蜂鸣器状态 */
    bool buzzer_value;          /**< 蜂鸣器状态值 (true: 开启, false: 关闭) */
    bool relay_set;             /**< 是否设置继电器状态 */
    bool relay_value;           /**< 继电器状态值 (true: 开启, false: 关闭) */
} device_cmd_t;

/**
 * @brief 使用项目默认值填充状态结构体。
 *
 * @param status 输出参数，接收初始化后的状态结构体。
 */
void device_status_init_default(device_status_t *status);

/**
 * @brief 初始化模块内部维护的全局状态快照。
 */
void device_status_store_init(void);

/**
 * @brief 将最新的全局状态复制到调用方提供的结构体中。
 *
 * @param status 输出参数，接收当前状态快照。
 */
void device_status_get(device_status_t *status);

/**
 * @brief 更新全局状态中的传感器相关字段。
 *
 * @param temperature 最新温度值。
 * @param humidity 最新湿度值。
 * @param light 最新光照值。
 */
void device_status_update_sensor(float temperature, float humidity, float light);

/**
 * @brief 将控制命令应用到全局状态快照中。
 *
 * @param cmd 输入参数，描述需要更新的执行器目标状态。
 */
void device_status_update_control(const device_cmd_t *cmd);

/**
 * @brief 刷新网络连接标志，并推导当前设备高层状态。
 *
 * @param wifi_connected Wi-Fi 链路是否已连接。
 * @param mqtt_connected MQTT 会话是否已连接。
 */
void device_status_update_network(bool wifi_connected, bool mqtt_connected);

/**
 * @brief 强制设置当前的运行状态。
 * 
 * @param state 目标运行状态。
 */
void device_status_set_state(device_state_t state);

/**
 * @brief 设置系统错误码并自动推导运行状态。
 * 
 * @param error_code 错误码（APP_ERR_NONE 表示清除错误）。
 */
void device_status_set_error(uint32_t error_code);

void device_status_clear_error(uint32_t error_code);

/**
 * @brief 增加系统累计运行时长。
 *
 * @param seconds 需要累加的秒数。
 */
void device_status_tick(uint32_t seconds);

#endif
