#include "device_control.h"

#include "board.h"
#include "driver/gpio.h"

static bool s_led_on;
static bool s_buzzer_on;
static bool s_relay_on;

esp_err_t device_control_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = BOARD_OUTPUT_GPIO_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }

    s_led_on = false;
    s_buzzer_on = false;
    s_relay_on = false;
    gpio_set_level(BOARD_LED_GPIO, 0);
    gpio_set_level(BOARD_BUZZER_GPIO, 0);
    gpio_set_level(BOARD_RELAY_GPIO, 0);
    return ESP_OK;
}

esp_err_t device_led_set(bool on)
{
    s_led_on = on;
    return gpio_set_level(BOARD_LED_GPIO, on ? 1 : 0);
}

esp_err_t device_buzzer_set(bool on)
{
    s_buzzer_on = on;
    return gpio_set_level(BOARD_BUZZER_GPIO, on ? 1 : 0);
}

esp_err_t device_relay_set(bool on)
{
    s_relay_on = on;
    return gpio_set_level(BOARD_RELAY_GPIO, on ? 1 : 0);
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
