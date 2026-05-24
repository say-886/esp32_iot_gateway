#include "device_status.h"

#include <string.h>

#include "error_code.h"

#define DEVICE_STATUS_DEFAULT_FIRMWARE "v0.1.0-prep"

static device_status_t s_status;

/**
 * @brief 使用适合演示网关的默认值初始化状态结构体。
 *
 * @param status 输出参数，接收初始化后的状态结构体。
 */
void device_status_init_default(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->temperature = 26.5f;
    status->humidity = 60.2f;
    status->light = 380.0f;
    status->error_code = APP_ERR_NONE;
    status->state = DEVICE_STATE_INIT;
    strncpy(status->firmware_version,
            DEVICE_STATUS_DEFAULT_FIRMWARE,
            sizeof(status->firmware_version) - 1);
}

/**
 * @brief 初始化模块内部维护的全局状态快照。
 */
void device_status_store_init(void)
{
    device_status_init_default(&s_status);
}

/**
 * @brief 将最新全局状态复制给调用方。
 *
 * @param status 输出参数，接收当前状态快照。
 */
void device_status_get(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    *status = s_status;
}

/**
 * @brief 更新共享设备状态中的传感器字段。
 *
 * @param temperature 最新温度值。
 * @param humidity 最新湿度值。
 * @param light 最新光照值。
 */
void device_status_update_sensor(float temperature, float humidity, float light)
{
    s_status.temperature = temperature;
    s_status.humidity = humidity;
    s_status.light = light;
}

/**
 * @brief 将请求的执行器状态写入共享设备状态。
 *
 * @param cmd 输入参数，描述需要更新的执行器目标状态。
 */
void device_status_update_control(const device_cmd_t *cmd)
{
    if (cmd == NULL) {
        return;
    }

    if (cmd->led_set) {
        s_status.led_on = cmd->led_value;
    }
    if (cmd->buzzer_set) {
        s_status.buzzer_on = cmd->buzzer_value;
    }
    if (cmd->relay_set) {
        s_status.relay_on = cmd->relay_value;
    }
}

/**
 * @brief 刷新网络连接标志，并推导设备的粗粒度运行状态。
 *
 * @param wifi_connected Wi-Fi 是否已连接。
 * @param mqtt_connected MQTT 是否已连接。
 */
void device_status_update_network(bool wifi_connected, bool mqtt_connected)
{
    s_status.wifi_connected = wifi_connected;
    s_status.mqtt_connected = mqtt_connected;

    /* 将连接进度映射为统一状态，便于界面和接口直接使用。 */
    if (mqtt_connected) {
        s_status.state = DEVICE_STATE_ONLINE;
    } else if (wifi_connected) {
        s_status.state = DEVICE_STATE_MQTT_CONNECTING;
    } else {
        s_status.state = DEVICE_STATE_WIFI_CONNECTING;
    }
}

/**
 * @brief 增加系统累计运行时间计数。
 *
 * @param seconds 需要累加的秒数。
 */
void device_status_tick(uint32_t seconds)
{
    s_status.uptime_sec += seconds;
}
