#include "device_status.h"

#include <stddef.h>
#include <string.h>

#include "error_code.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DEVICE_STATUS_DEFAULT_FIRMWARE "v0.1.0-prep"

static device_status_t s_status;
static SemaphoreHandle_t s_status_mutex;

/**
 * @brief 锁定设备状态结构体。
 *
 * 该函数会在修改设备状态结构体之前，先获取互斥锁，确保线程安全。
 */
static void status_lock(void)
{
    if (s_status_mutex != NULL) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

/**
 * @brief 解锁设备状态结构体。
 *
 * 该函数会在修改设备状态结构体后，释放互斥锁，确保其他线程可以访问设备状态。
 */
static void status_unlock(void)
{
    if (s_status_mutex != NULL) {
        xSemaphoreGive(s_status_mutex);
    }
}

/**
 * @brief 检查是否为传感器错误。
 *
 * 该函数会根据错误码判断是否为传感器错误，即是否为 AHT20 或 BH1750 读取错误。
 *
 * @param error_code 错误码。
 * @return 是否为传感器错误。
 */
static bool is_sensor_error(uint32_t error_code)
{
    return error_code == APP_ERR_AHT20_READ_FAILED ||
           error_code == APP_ERR_BH1750_READ_FAILED;
}

/**
 * @brief 获取错误码对应的错误标志位。
 *
 * 该函数会根据错误码返回对应的错误标志位，用于在设备状态结构体中记录错误。
 *
 * @param error_code 错误码。
 * @return 错误标志位。
 */
static uint32_t error_flag(uint32_t error_code)
{
    switch (error_code) {
    case APP_ERR_WIFI_CONNECT_FAILED: return 1U << 0;
    case APP_ERR_MQTT_CONNECT_FAILED: return 1U << 1;
    case APP_ERR_AHT20_READ_FAILED: return 1U << 2;
    case APP_ERR_BH1750_READ_FAILED: return 1U << 3;
    case APP_ERR_NVS_READ_FAILED: return 1U << 4;
    case APP_ERR_NVS_WRITE_FAILED: return 1U << 5;
    case APP_ERR_OTA_FAILED: return 1U << 6;
    case APP_ERR_WATCHDOG: return 1U << 7;
    case APP_ERR_MODBUS_READ_FAILED: return 1U << 8;
    default: return 0;
    }
}

/**
 * @brief 获取当前设备状态的主错误码。
 *
 * 该函数会根据当前记录的错误标志位，返回优先级最高的错误码。
 *
 * @return 当前设备状态的主错误码。
 */
static uint32_t primary_error(void)
{
    static const uint32_t priority[] = {
        APP_ERR_WATCHDOG,
        APP_ERR_MODBUS_READ_FAILED,
        APP_ERR_OTA_FAILED,
        APP_ERR_NVS_WRITE_FAILED,
        APP_ERR_NVS_READ_FAILED,
        APP_ERR_WIFI_CONNECT_FAILED,
        APP_ERR_MQTT_CONNECT_FAILED,
        APP_ERR_AHT20_READ_FAILED,
        APP_ERR_BH1750_READ_FAILED,
    };

    for (size_t i = 0; i < sizeof(priority) / sizeof(priority[0]); i++) {
        if ((s_status.error_flags & error_flag(priority[i])) != 0U) {
            return priority[i];
        }
    }
    return APP_ERR_NONE;
}

/**
 * @brief 恢复设备状态到网络连接。
 *
 * 该函数会根据当前记录的网络连接状态，将设备状态恢复到对应的在线或连接中状态。
 */
static void restore_state_from_network(void)
{
    if (s_status.mqtt_connected) {
        s_status.state = DEVICE_STATE_ONLINE;
    } else if (s_status.wifi_connected) {
        s_status.state = DEVICE_STATE_MQTT_CONNECTING;
    } else {
        s_status.state = DEVICE_STATE_WIFI_CONNECTING;
    }
}

/**
 * @brief 刷新设备状态。
 *
 * 该函数会根据当前记录的错误码，刷新设备状态。
 * 如果错误码为 `APP_ERR_NONE`，则恢复设备状态到网络连接。
 * 如果错误码为传感器错误，将设备状态设置为 `DEVICE_STATE_RECOVERY`。
 * 否则，将设备状态设置为 `DEVICE_STATE_ERROR`。
 */
static void refresh_state(void)
{
    s_status.error_code = primary_error();
    if (s_status.error_code == APP_ERR_NONE) {
        restore_state_from_network();
    } else if (is_sensor_error(s_status.error_code)) {
        s_status.state = DEVICE_STATE_RECOVERY;
    } else {
        s_status.state = DEVICE_STATE_ERROR;
    }
}

/**
 * @brief 初始化设备状态结构体为默认值。
 *
 * 该函数会将设备状态结构体的所有字段设置为默认值，包括温度、湿度、光照、状态、错误码、错误标志位、是否连接到 WiFi、是否连接到 MQTT 服务器、是否打开 LED、是否打开蜂鸣器、是否打开继电器。
 *
 * @param status 设备状态结构体的指针。
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
    status->state = DEVICE_STATE_INIT;
    strncpy(status->firmware_version,
            DEVICE_STATUS_DEFAULT_FIRMWARE,
            sizeof(status->firmware_version) - 1);
}

/**
 * @brief 初始化设备状态存储。
 *
 * 该函数会创建设备状态存储的互斥锁，确保线程安全。
 * 并将设备状态结构体初始化为默认值。
 */
void device_status_store_init(void)
{
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutex();
        configASSERT(s_status_mutex != NULL);
    }
    status_lock();              // 锁定设备状态结构体
    device_status_init_default(&s_status);
    status_unlock();
}

/**
 * @brief 获取当前设备状态。
 *
 * 该函数会将当前设备状态复制到指定的设备状态结构体的指针。
 *
 * @param status 设备状态结构体的指针。
 */
void device_status_get(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status_lock();
    *status = s_status;
    status_unlock();
}

/**
 * @brief 更新传感器数据。
 *
 * 该函数会根据指定的温度、湿度、光照值，更新设备状态结构体的对应字段。
 *
 * @param temperature 温度值。
 * @param humidity 湿度值。
 * @param light 光照值。
 */
void device_status_update_sensor(float temperature, float humidity, float light)
{
    status_lock();
    s_status.temperature = temperature;
    s_status.humidity = humidity;
    s_status.light = light;
    status_unlock();
}

/**
 * @brief 更新控制命令。
 *
 * 该函数会根据指定的控制命令，更新设备状态结构体的对应字段。
 *
 * @param cmd 控制命令的指针。
 */
void device_status_update_control(const device_cmd_t *cmd)
{
    if (cmd == NULL) {
        return;
    }

    status_lock();
    if (cmd->led_set) {
        s_status.led_on = cmd->led_value;
    }
    if (cmd->buzzer_set) {
        s_status.buzzer_on = cmd->buzzer_value;
    }
    if (cmd->relay_set) {
        s_status.relay_on = cmd->relay_value;
    }
    status_unlock();
}

/**
 * @brief 更新网络连接状态。
 *
 * 该函数会根据指定的 WiFi 连接状态和 MQTT 连接状态，更新设备状态结构体的对应字段。
 *
 * @param wifi_connected 是否 WiFi 连接成功。
 * @param mqtt_connected 是否 MQTT 连接成功。
 */
void device_status_update_network(bool wifi_connected, bool mqtt_connected)
{
    status_lock();
    s_status.wifi_connected = wifi_connected;
    s_status.mqtt_connected = mqtt_connected;
    if (wifi_connected) {
        s_status.error_flags &= ~error_flag(APP_ERR_WIFI_CONNECT_FAILED);
    }
    if (mqtt_connected) {
        s_status.error_flags &= ~error_flag(APP_ERR_MQTT_CONNECT_FAILED);
    }
    refresh_state();
    status_unlock();
}

/**
 * @brief 设置设备状态。
 *
 * 该函数会将指定的设备状态赋值给当前设备状态。
 *
 * @param state 设备状态。
 */
void device_status_set_state(device_state_t state)
{
    status_lock();
    s_status.state = state;
    status_unlock();
}

/**
 * @brief 设置错误码。
 *
 * 该函数会将指定的错误码赋值给当前设备状态的错误码字段。
 *
 * @param error_code 错误码。
 */
void device_status_set_error(uint32_t error_code)
{
    status_lock();
    if (error_code != APP_ERR_NONE) {
        s_status.error_flags |= error_flag(error_code);
    }
    refresh_state();
    status_unlock();
}

/**
 * @brief 清除错误码。
 *
 * 该函数会将指定的错误码从当前设备状态的错误码字段中清除。
 *
 * @param error_code 错误码。
 */
void device_status_clear_error(uint32_t error_code)
{
    status_lock();
    s_status.error_flags &= ~error_flag(error_code);
    refresh_state();
    status_unlock();
}

/**
 * @brief 更新设备状态的运行时间。
 *
 * 该函数会将指定的秒数加到当前设备状态的运行时间字段中。
 *
 * @param seconds 秒数。
 */
void device_status_tick(uint32_t seconds)
{
    status_lock();
    s_status.uptime_sec += seconds;
    status_unlock();
}
