#include "button.h"

#include <stdint.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define BUTTON_LONG_PRESS_MS 1000

typedef struct {
    gpio_num_t gpio;
    bool stable_pressed;
    bool last_raw_pressed;
    int64_t pressed_at_ms;
    bool long_reported;
} button_runtime_t;

static button_runtime_t s_buttons[BUTTON_ID_MAX] = {
    [BUTTON_ID_K1] = {.gpio = BOARD_BUTTON_1_GPIO},
    [BUTTON_ID_K2] = {.gpio = BOARD_BUTTON_2_GPIO},
    [BUTTON_ID_K3] = {.gpio = BOARD_BUTTON_3_GPIO},
    [BUTTON_ID_K4] = {.gpio = BOARD_BUTTON_4_GPIO},
};

static int64_t button_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

esp_err_t button_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = BOARD_BUTTON_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        s_buttons[i].stable_pressed = false;
        s_buttons[i].last_raw_pressed = false;
        s_buttons[i].pressed_at_ms = 0;
        s_buttons[i].long_reported = false;
    }
    return ESP_OK;
}

button_event_t button_scan_key(button_id_t button)
{
    if (button < 0 || button >= BUTTON_ID_MAX) {
        return BUTTON_EVENT_NONE;
    }

    button_runtime_t *runtime = &s_buttons[button];
    bool raw_pressed = gpio_get_level(runtime->gpio) == 0;
    int64_t now = button_now_ms();

    if (raw_pressed && !runtime->stable_pressed) {
        runtime->stable_pressed = true;
        runtime->pressed_at_ms = now;
        runtime->long_reported = false;
        runtime->last_raw_pressed = raw_pressed;
        return BUTTON_EVENT_NONE;
    }

    if (raw_pressed && runtime->stable_pressed && !runtime->long_reported &&
        now - runtime->pressed_at_ms >= BUTTON_LONG_PRESS_MS) {
        runtime->long_reported = true;
        runtime->last_raw_pressed = raw_pressed;
        return BUTTON_EVENT_LONG_PRESS;
    }

    if (!raw_pressed && runtime->stable_pressed) {
        bool was_long = runtime->long_reported;
        runtime->stable_pressed = false;
        runtime->pressed_at_ms = 0;
        runtime->long_reported = false;
        runtime->last_raw_pressed = raw_pressed;
        return was_long ? BUTTON_EVENT_NONE : BUTTON_EVENT_SHORT_PRESS;
    }

    runtime->last_raw_pressed = raw_pressed;
    return BUTTON_EVENT_NONE;
}

button_event_t button_scan(void)
{
    return button_scan_key(BUTTON_ID_K1);
}

bool button_is_key_pressed(button_id_t button)
{
    if (button < 0 || button >= BUTTON_ID_MAX) {
        return false;
    }
    return gpio_get_level(s_buttons[button].gpio) == 0;
}

bool button_is_pressed(void)
{
    return button_is_key_pressed(BUTTON_ID_K1);
}
