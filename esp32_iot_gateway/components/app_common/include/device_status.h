#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

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

void device_status_set_state(device_state_t state);
void device_status_set_error(uint32_t error_code);

/**
 * @brief 增加系统累计运行时长。
 *
 * @param seconds 需要累加的秒数。
 */
void device_status_tick(uint32_t seconds);

#endif
