#include "device_control.h"

static bool s_led_on;
static bool s_buzzer_on;
static bool s_relay_on;

esp_err_t device_control_init(void)
{
    s_led_on = false;
    s_buzzer_on = false;
    s_relay_on = false;
    return ESP_OK;
}

esp_err_t device_led_set(bool on)
{
    s_led_on = on;
    return ESP_OK;
}

esp_err_t device_buzzer_set(bool on)
{
    s_buzzer_on = on;
    return ESP_OK;
}

esp_err_t device_relay_set(bool on)
{
    s_relay_on = on;
    return ESP_OK;
}

bool device_led_get(void)
{
    return s_led_on;
}

bool device_buzzer_get(void)
{
    return s_buzzer_on;
}

bool device_relay_get(void)
{
    return s_relay_on;
}
