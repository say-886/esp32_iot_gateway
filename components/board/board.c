#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

static esp_err_t board_i2c_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(BOARD_I2C_PORT, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(BOARD_I2C_PORT, config.mode, 0, 0, 0);
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

esp_err_t board_init(void)
{
    gpio_config_t input_config = {
        .pin_bit_mask = BOARD_BUTTON_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&input_config);
    if (ret != ESP_OK) {
        return ret;
    }

    return board_i2c_init();
}
