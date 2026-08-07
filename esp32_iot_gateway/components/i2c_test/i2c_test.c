#include "i2c_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_TEST_PORT I2C_NUM_0
#define I2C_TEST_FREQ_HZ 100000
#define I2C_TEST_TIMEOUT_MS 1000
#define I2C_TEST_AHT20_ADDR 0x38
#define I2C_TEST_BH1750_ADDR_LOW 0x23
#define I2C_TEST_BH1750_ADDR_HIGH 0x5C

static const char *TAG = "i2c_test";

static esp_err_t i2c_test_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        i2c_cmd_link_delete(cmd);
        return ret;
    }

    ret = i2c_master_cmd_begin(I2C_TEST_PORT, cmd, pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));

    board_i2c_unlock();
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void i2c_test_scan(bool *found_aht20, bool *found_bh1750_low, bool *found_bh1750_high)
{
    *found_aht20 = false;
    *found_bh1750_low = false;
    *found_bh1750_high = false;

    ESP_LOGI(TAG, "Scanning I2C bus: SDA=GPIO%d, SCL=GPIO%d",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (i2c_test_probe(addr) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
            if (addr == I2C_TEST_AHT20_ADDR) {
                *found_aht20 = true;
            } else if (addr == I2C_TEST_BH1750_ADDR_LOW) {
                *found_bh1750_low = true;
            } else if (addr == I2C_TEST_BH1750_ADDR_HIGH) {
                *found_bh1750_high = true;
            }
        }
    }
}

static esp_err_t bh1750_read_lux(uint8_t addr, float *lux)
{
    const uint8_t cmd = 0x10;

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_write_to_device(I2C_TEST_PORT, addr, &cmd, 1,
                                     pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        board_i2c_unlock();
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(180));

    uint8_t data[2] = {0};
    ret = i2c_master_read_from_device(I2C_TEST_PORT, addr, data, sizeof(data),
                                      pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));

    board_i2c_unlock();

    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *lux = (float)raw / 1.2f;
    return ESP_OK;
}

static esp_err_t aht20_read(float *temperature, float *humidity)
{
    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t status = 0;
    ret = i2c_master_read_from_device(I2C_TEST_PORT, I2C_TEST_AHT20_ADDR,
                                      &status, 1,
                                      pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        board_i2c_unlock();
        return ret;
    }

    if ((status & 0x18) != 0x18) {
        const uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
        ret = i2c_master_write_to_device(I2C_TEST_PORT, I2C_TEST_AHT20_ADDR,
                                         init_cmd, sizeof(init_cmd),
                                         pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));
        if (ret != ESP_OK) {
            board_i2c_unlock();
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    const uint8_t measure_cmd[3] = {0xAC, 0x33, 0x00};
    ret = i2c_master_write_to_device(I2C_TEST_PORT, I2C_TEST_AHT20_ADDR,
                                     measure_cmd, sizeof(measure_cmd),
                                     pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        board_i2c_unlock();
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t data[6] = {0};
    ret = i2c_master_read_from_device(I2C_TEST_PORT, I2C_TEST_AHT20_ADDR,
                                      data, sizeof(data),
                                      pdMS_TO_TICKS(I2C_TEST_TIMEOUT_MS));

    board_i2c_unlock();

    if (ret != ESP_OK) {
        return ret;
    }

    if ((data[0] & 0x80) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t raw_humidity = ((uint32_t)data[1] << 12) |
                            ((uint32_t)data[2] << 4) |
                            ((uint32_t)data[3] >> 4);
    uint32_t raw_temperature = (((uint32_t)data[3] & 0x0F) << 16) |
                               ((uint32_t)data[4] << 8) |
                               data[5];

    *humidity = ((float)raw_humidity * 100.0f) / 1048576.0f;
    *temperature = (((float)raw_temperature * 200.0f) / 1048576.0f) - 50.0f;
    return ESP_OK;
}

static void i2c_test_task(void *arg)
{
    (void)arg;

    while (true) {
        bool found_aht20 = false;
        bool found_bh1750_low = false;
        bool found_bh1750_high = false;
        i2c_test_scan(&found_aht20, &found_bh1750_low, &found_bh1750_high);

        if (!found_aht20) {
            ESP_LOGW(TAG, "AHT20 not found. Expected address: 0x38");
        } else {
            float temperature = 0.0f;
            float humidity = 0.0f;
            esp_err_t ret = aht20_read(&temperature, &humidity);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "AHT20 OK: temperature=%.2f C, humidity=%.2f %%RH",
                         temperature, humidity);
            } else {
                ESP_LOGW(TAG, "AHT20 found but read failed: %s", esp_err_to_name(ret));
            }
        }

        uint8_t bh1750_addr = 0;
        if (found_bh1750_low) {
            bh1750_addr = I2C_TEST_BH1750_ADDR_LOW;
        } else if (found_bh1750_high) {
            bh1750_addr = I2C_TEST_BH1750_ADDR_HIGH;
        }

        if (bh1750_addr == 0) {
            ESP_LOGW(TAG, "BH1750 not found. Expected address: 0x23 or 0x5C");
        } else {
            float lux = 0.0f;
            esp_err_t ret = bh1750_read_lux(bh1750_addr, &lux);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "BH1750 OK: addr=0x%02X, light=%.2f lux",
                         bh1750_addr, lux);
            } else {
                ESP_LOGW(TAG, "BH1750 found but read failed: %s", esp_err_to_name(ret));
            }
        }

        ESP_LOGI(TAG, "I2C test loop finished. Next scan in 5 seconds.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t i2c_test_start(void)
{
    BaseType_t created = xTaskCreate(i2c_test_task, "i2c_test", 4096, NULL, 5, NULL);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
