#ifndef BOARD_H
#define BOARD_H

#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_FREQ_HZ 100000
#define BOARD_I2C_SDA_GPIO GPIO_NUM_21
#define BOARD_I2C_SCL_GPIO GPIO_NUM_22
#define BOARD_AHT20_I2C_ADDR 0x38
#define BOARD_BH1750_I2C_ADDR 0x23
#define BOARD_OLED_I2C_ADDR_PRIMARY 0x3C    // 常见的 OLED I2C 地址
#define BOARD_OLED_I2C_ADDR_SECONDARY 0x3D   // 一些 OLED 模块使用这个地址
#define BOARD_LED_GPIO GPIO_NUM_2
#define BOARD_LED_1_GPIO GPIO_NUM_2
#define BOARD_BUZZER_GPIO GPIO_NUM_25
#define BOARD_RELAY_GPIO GPIO_NUM_26
#define BOARD_LED_ACTIVE_LEVEL 1
#define BOARD_BUZZER_ACTIVE_LEVEL 1
#define BOARD_RELAY_ACTIVE_LEVEL 1
#define BOARD_BUTTON_1_GPIO GPIO_NUM_27
#define BOARD_BUTTON_2_GPIO GPIO_NUM_14
#define BOARD_BUTTON_3_GPIO GPIO_NUM_32
#define BOARD_BUTTON_4_GPIO GPIO_NUM_33
#define BOARD_RS485_UART UART_NUM_2
#define BOARD_RS485_TX_GPIO GPIO_NUM_17
#define BOARD_RS485_RX_GPIO GPIO_NUM_16
#define BOARD_RS485_RTS_GPIO GPIO_NUM_4

#define BOARD_OUTPUT_GPIO_MASK ((1ULL << BOARD_LED_GPIO) | \
                                (1ULL << BOARD_BUZZER_GPIO) | \
                                (1ULL << BOARD_RELAY_GPIO))      // 输出 GPIO 掩码

#define BOARD_BUTTON_GPIO_MASK ((1ULL << BOARD_BUTTON_1_GPIO) | \
                                (1ULL << BOARD_BUTTON_2_GPIO) | \
                                (1ULL << BOARD_BUTTON_3_GPIO) | \
                                (1ULL << BOARD_BUTTON_4_GPIO))      // 按钮 GPIO 掩码

esp_err_t board_init(void);
esp_err_t board_i2c_lock(void);
void board_i2c_unlock(void);

#endif
