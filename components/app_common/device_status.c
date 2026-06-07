#include "device_status.h"

#include <stddef.h>
#include <string.h>

#include "error_code.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DEVICE_STATUS_DEFAULT_FIRMWARE "v0.1.0-prep"

static device_status_t s_status;
static SemaphoreHandle_t s_status_mutex;

static void status_lock(void)
{
    if (s_status_mutex != NULL) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_status_mutex != NULL) {
        xSemaphoreGive(s_status_mutex);
    }
}

static bool is_sensor_error(uint32_t error_code)
{
    return error_code == APP_ERR_AHT20_READ_FAILED ||
           error_code == APP_ERR_BH1750_READ_FAILED;
}

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
    default: return 0;
    }
}

static uint32_t primary_error(void)
{
    static const uint32_t priority[] = {
        APP_ERR_WATCHDOG,
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

void device_status_store_init(void)
{
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutex();
        configASSERT(s_status_mutex != NULL);
    }
    status_lock();
    device_status_init_default(&s_status);
    status_unlock();
}

void device_status_get(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status_lock();
    *status = s_status;
    status_unlock();
}

void device_status_update_sensor(float temperature, float humidity, float light)
{
    status_lock();
    s_status.temperature = temperature;
    s_status.humidity = humidity;
    s_status.light = light;
    status_unlock();
}

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

void device_status_set_state(device_state_t state)
{
    status_lock();
    s_status.state = state;
    status_unlock();
}

void device_status_set_error(uint32_t error_code)
{
    status_lock();
    if (error_code != APP_ERR_NONE) {
        s_status.error_flags |= error_flag(error_code);
    }
    refresh_state();
    status_unlock();
}

void device_status_clear_error(uint32_t error_code)
{
    status_lock();
    s_status.error_flags &= ~error_flag(error_code);
    refresh_state();
    status_unlock();
}

void device_status_tick(uint32_t seconds)
{
    status_lock();
    s_status.uptime_sec += seconds;
    status_unlock();
}
