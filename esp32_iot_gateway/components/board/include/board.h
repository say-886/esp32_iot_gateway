#ifndef BOARD_H
#define BOARD_H

#include "esp_err.h"
#include "hal/gpio_types.h"

#define BOARD_I2C_SDA_GPIO GPIO_NUM_21
#define BOARD_I2C_SCL_GPIO GPIO_NUM_22
#define BOARD_LED_GPIO GPIO_NUM_2
#define BOARD_BUZZER_GPIO GPIO_NUM_25
#define BOARD_RELAY_GPIO GPIO_NUM_26
#define BOARD_BUTTON_1_GPIO GPIO_NUM_0

#define BOARD_OUTPUT_GPIO_MASK ((1ULL << BOARD_LED_GPIO) | \
                                (1ULL << BOARD_BUZZER_GPIO) | \
                                (1ULL << BOARD_RELAY_GPIO))

esp_err_t board_init(void);

#endif
