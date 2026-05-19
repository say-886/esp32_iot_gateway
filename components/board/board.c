#include "board.h"

#include "driver/gpio.h"

esp_err_t board_init(void)
{
    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << BOARD_BUTTON_1_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&input_config);
}
