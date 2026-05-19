#include "device_status.h"

#include <string.h>

#include "error_code.h"

#define DEVICE_STATUS_DEFAULT_FIRMWARE "v0.1.0-prep"

static device_status_t s_status;

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

void device_status_store_init(void)
{
    device_status_init_default(&s_status);
}

void device_status_get(device_status_t *status)
{
    if (status == NULL) {
        return;
    }

    *status = s_status;
}

void device_status_update_sensor(float temperature, float humidity, float light)
{
    s_status.temperature = temperature;
    s_status.humidity = humidity;
    s_status.light = light;
}

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

void device_status_update_network(bool wifi_connected, bool mqtt_connected)
{
    s_status.wifi_connected = wifi_connected;
    s_status.mqtt_connected = mqtt_connected;
    if (mqtt_connected) {
        s_status.state = DEVICE_STATE_ONLINE;
    } else if (wifi_connected) {
        s_status.state = DEVICE_STATE_MQTT_CONNECTING;
    } else {
        s_status.state = DEVICE_STATE_WIFI_CONNECTING;
    }
}

void device_status_tick(uint32_t seconds)
{
    s_status.uptime_sec += seconds;
}
