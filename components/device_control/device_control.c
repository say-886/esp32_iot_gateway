#include "device_control.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"

static bool s_led_on;
static bool s_buzzer_on;
static bool s_relay_on;
static const char *TAG = "device_control";

static int output_level_from_state(bool on, int active_level)
{
    return on ? active_level : !active_level;
}

/**
 * @brief 初始化设备控制 GPIO 引脚。
 *
 * @return esp_err_t ESP_OK 成功，其他错误码失败。
 */
esp_err_t device_control_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = BOARD_OUTPUT_GPIO_MASK, // 配置输出 GPIO 引脚为输出模式
        .mode = GPIO_MODE_OUTPUT,               // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,      // 禁用上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 禁用下拉电阻
        .intr_type = GPIO_INTR_DISABLE,         // 禁用中断
    };
    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }

    s_led_on = false;
    s_buzzer_on = false;
    s_relay_on = false;
    gpio_set_level(BOARD_LED_GPIO, output_level_from_state(false, BOARD_LED_ACTIVE_LEVEL));
    gpio_set_level(BOARD_BUZZER_GPIO, output_level_from_state(false, BOARD_BUZZER_ACTIVE_LEVEL));
    gpio_set_level(BOARD_RELAY_GPIO, output_level_from_state(false, BOARD_RELAY_ACTIVE_LEVEL));
    return ESP_OK;
}

/**
 * @brief 设置 LED 状态。
 *
 * @param on LED 状态，true 表示亮，false 表示灭。
 * @return esp_err_t ESP_OK 成功，其他错误码失败。
 */
esp_err_t device_led_set(bool on)
{
    s_led_on = on;
    ESP_LOGI(TAG, "set LED: on=%d gpio=%d level=%d",
             on ? 1 : 0, BOARD_LED_GPIO,
             output_level_from_state(on, BOARD_LED_ACTIVE_LEVEL));
    return gpio_set_level(BOARD_LED_GPIO, output_level_from_state(on, BOARD_LED_ACTIVE_LEVEL));
}

/**
 * @brief 设置蜂鸣器状态。
 *
 * @param on 蜂鸣器状态，true 表示蜂鸣，false 表示静音。
 * @return esp_err_t ESP_OK 成功，其他错误码失败。
 */
esp_err_t device_buzzer_set(bool on)
{
    s_buzzer_on = on;
    ESP_LOGI(TAG, "set buzzer: on=%d gpio=%d level=%d",
             on ? 1 : 0, BOARD_BUZZER_GPIO,
             output_level_from_state(on, BOARD_BUZZER_ACTIVE_LEVEL));
    return gpio_set_level(BOARD_BUZZER_GPIO, output_level_from_state(on, BOARD_BUZZER_ACTIVE_LEVEL));
}

/**
 * @brief 设置继电器状态。
 *
 * @param on 继电器状态，true 表示闭合，false 表示开。
 * @return esp_err_t ESP_OK 成功，其他错误码失败。
 */
esp_err_t device_relay_set(bool on)
{
    s_relay_on = on;
    ESP_LOGI(TAG, "set relay: on=%d gpio=%d level=%d",
             on ? 1 : 0, BOARD_RELAY_GPIO,
             output_level_from_state(on, BOARD_RELAY_ACTIVE_LEVEL));
    return gpio_set_level(BOARD_RELAY_GPIO, output_level_from_state(on, BOARD_RELAY_ACTIVE_LEVEL));
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
