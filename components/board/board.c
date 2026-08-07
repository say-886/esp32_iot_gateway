#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t s_i2c_mutex;

/**
 * @brief 加锁 I2C 总线。
 *
 * 确保在 I2C 操作期间没有其他线程或任务访问 I2C 总线。
 *
 * @return esp_err_t ESP_OK 成功，ESP_ERR_TIMEOUT 超时。
 */
esp_err_t board_i2c_lock(void)
{
    if (s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTakeRecursive(s_i2c_mutex, pdMS_TO_TICKS(2000)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

/**
 * @brief 解锁 I2C 总线。
 *
 * 释放 I2C 总线，允许其他线程或任务访问。
 */
void board_i2c_unlock(void)
{
    if (s_i2c_mutex != NULL) {
        xSemaphoreGiveRecursive(s_i2c_mutex);
    }
}

/**
 * @brief 初始化板载 I2C 总线控制器。
 *
 * 配置 I2C 模式（主站）、引脚、频率等。
 *
 *
 * @return esp_err_t ESP_OK 成功。
 */
static esp_err_t board_i2c_init(void)
{

    // 初始化互斥锁，确保线程安全访问
    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_i2c_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    // 配置 I2C 模式（主站）、引脚、频率等
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

/**
 * @brief 初始化板载基础硬件资源。
 *
 * 包括：
 * 1. 配置按键 GPIO 为输入模式并使能上拉。
 * 2. 调用 I2C 初始化。
 *
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t board_init(void)
{
    // 配置按键 GPIO 为输入模式并使能上拉电阻
    gpio_config_t input_config = {
        .pin_bit_mask = BOARD_BUTTON_GPIO_MASK, // 配置按键 GPIO 为输入模式
        .mode = GPIO_MODE_INPUT,                // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,        // 使能上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE,   // 禁用下拉电阻
        .intr_type = GPIO_INTR_DISABLE,          // 禁用中断
    };

    esp_err_t ret = gpio_config(&input_config);
    if (ret != ESP_OK) {
        return ret;
    }

    return board_i2c_init();
}
