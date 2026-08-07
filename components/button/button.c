#include "button.h"

#include <stdint.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define BUTTON_LONG_PRESS_MS 1000
#define BUTTON_DEBOUNCE_MS 30

typedef struct {
    gpio_num_t gpio;
    bool stable_pressed;      // 稳定按下状态
    bool last_raw_pressed;    // 上一次原始按下状态
    int64_t raw_changed_at_ms;
    int64_t pressed_at_ms;    // 按下时间戳（毫秒）
    bool long_reported;       // 是否已报告长按事件
} button_runtime_t;

static button_runtime_t s_buttons[BUTTON_ID_MAX] = {
    [BUTTON_ID_K1] = {.gpio = BOARD_BUTTON_1_GPIO},
    [BUTTON_ID_K2] = {.gpio = BOARD_BUTTON_2_GPIO},
    [BUTTON_ID_K3] = {.gpio = BOARD_BUTTON_3_GPIO},
    [BUTTON_ID_K4] = {.gpio = BOARD_BUTTON_4_GPIO},
};

/**
 * @brief 获取当前时间戳（毫秒）。
 *
 * @return int64_t 当前时间戳（毫秒）。
 */
static int64_t button_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/**
 * @brief 初始化按键输入层（4 个按键）。
 *
 * @return esp_err_t ESP_OK 成功，其他错误码失败。
 */
esp_err_t button_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = BOARD_BUTTON_GPIO_MASK,  // 配置按键 GPIO 为输入模式
        .mode = GPIO_MODE_INPUT,                 // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,        // 使能上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE,   // 禁用下拉电阻
        .intr_type = GPIO_INTR_DISABLE,          // 禁用中断
    };

    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        s_buttons[i].stable_pressed = false;
        s_buttons[i].last_raw_pressed = false;
        s_buttons[i].pressed_at_ms = 0;
        s_buttons[i].raw_changed_at_ms = button_now_ms();
        s_buttons[i].long_reported = false;
    }
    return ESP_OK;
}

/**
 * @brief 扫描指定按键的按下状态。
 *
 * @param button 按键 ID。
 * @return button_event_t 按键事件类型（如短按、长按、无事件）。
 */
button_event_t button_scan_key(button_id_t button)
{
    if (button < 0 || button >= BUTTON_ID_MAX) {
        return BUTTON_EVENT_NONE;
    }

    button_runtime_t *runtime = &s_buttons[button];
    bool raw_pressed = gpio_get_level(runtime->gpio) == 0;
    int64_t now = button_now_ms();

    if (raw_pressed != runtime->last_raw_pressed) {
        runtime->last_raw_pressed = raw_pressed;
        runtime->raw_changed_at_ms = now;
        return BUTTON_EVENT_NONE;
    }

    if (raw_pressed != runtime->stable_pressed &&
        now - runtime->raw_changed_at_ms >= BUTTON_DEBOUNCE_MS) {
        runtime->stable_pressed = raw_pressed;
        if (raw_pressed) {
            runtime->pressed_at_ms = now;
            runtime->long_reported = false;
            return BUTTON_EVENT_NONE;
        }

        bool was_long = runtime->long_reported;
        runtime->pressed_at_ms = 0;
        runtime->long_reported = false;
        return was_long ? BUTTON_EVENT_NONE : BUTTON_EVENT_SHORT_PRESS;
    }

    if (raw_pressed && runtime->stable_pressed && !runtime->long_reported &&
        now - runtime->pressed_at_ms >= BUTTON_LONG_PRESS_MS) {
        runtime->long_reported = true;
        return BUTTON_EVENT_LONG_PRESS;
    }
    return BUTTON_EVENT_NONE;
}

/**
 * @brief 扫描所有按键的按下状态，优先返回 K1 的事件。
 *
 * @return button_event_t 按键事件类型（如短按、长按、无事件）。
 */
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
