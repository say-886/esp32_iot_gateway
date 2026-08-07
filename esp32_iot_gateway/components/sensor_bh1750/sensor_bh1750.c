#include "sensor_bh1750.h"

#include <stdint.h>

#include "board.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BH1750_I2C_PORT I2C_NUM_0
#define BH1750_ADDR_LOW 0x23
#define BH1750_ADDR_HIGH 0x5C
#define BH1750_TIMEOUT_MS 1000
#define BH1750_CMD_CONT_H_RES 0x10

static uint8_t s_bh1750_addr = BH1750_ADDR_LOW;

/**
 * @brief 探测指定 I2C 地址是否存在 BH1750 传感器。
 * 
 * @param addr I2C 地址。
 * @return esp_err_t ESP_OK 存在且响应。
 */
static esp_err_t bh1750_probe(uint8_t addr)
{
    const uint8_t cmd = BH1750_CMD_CONT_H_RES;

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_write_to_device(BH1750_I2C_PORT, addr, &cmd, 1,
                                     pdMS_TO_TICKS(BH1750_TIMEOUT_MS));

    board_i2c_unlock();
    return ret;
}

/**
 * @brief 初始化 BH1750 传感器。
 * 
 * 自动探测 0x23 和 0x5C 两个可能的地址。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t bh1750_init(void)
{
    esp_err_t ret = bh1750_probe(BH1750_ADDR_LOW);
    if (ret == ESP_OK) {
        s_bh1750_addr = BH1750_ADDR_LOW;
        return ESP_OK;
    }

    ret = bh1750_probe(BH1750_ADDR_HIGH);
    if (ret == ESP_OK) {
        s_bh1750_addr = BH1750_ADDR_HIGH;
    }
    return ret;
}

/**
 * @brief 读取 BH1750 的光照强度数据。
 * 
 * @param light_lux 输出参数，存储光照强度值（Lux）。
 * @return esp_err_t ESP_OK 成功。
 */
esp_err_t bh1750_read(float *light_lux)
{
    if (light_lux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t cmd = BH1750_CMD_CONT_H_RES;
    ret = i2c_master_write_to_device(BH1750_I2C_PORT, s_bh1750_addr,
                                     &cmd, 1,
                                     pdMS_TO_TICKS(BH1750_TIMEOUT_MS));
    board_i2c_unlock();

    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(180));

    ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t data[2] = {0};
    ret = i2c_master_read_from_device(BH1750_I2C_PORT, s_bh1750_addr,
                                      data, sizeof(data),
                                      pdMS_TO_TICKS(BH1750_TIMEOUT_MS));

    board_i2c_unlock();

    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *light_lux = (float)raw / 1.2f;
    return ESP_OK;
}
