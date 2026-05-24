#include "sensor_aht20.h"

#include <stdint.h>

#include "board.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AHT20_TIMEOUT_MS 1000
#define AHT20_POWER_ON_DELAY_MS 80
#define AHT20_RESET_DELAY_MS 20
#define AHT20_INIT_DELAY_MS 20
#define AHT20_MEASURE_MAX_WAIT_MS 200
#define AHT20_MEASURE_POLL_DELAY_MS 10
#define AHT20_RETRY_COUNT 3
#define AHT20_RETRY_DELAY_MS 20
#define AHT20_STATUS_BUSY_BIT 0x80
#define AHT20_STATUS_CALIBRATED_MASK 0x18
#define AHT20_MEASURE_DATA_LEN 7

static esp_err_t aht20_write_command(const uint8_t *command, size_t command_len)
{
    if (command == NULL || command_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_to_device(BOARD_I2C_PORT,
                                      BOARD_AHT20_I2C_ADDR,
                                      command,
                                      command_len,
                                      pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
}

static esp_err_t aht20_read_status(uint8_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_read_from_device(BOARD_I2C_PORT,
                                       BOARD_AHT20_I2C_ADDR,
                                       status,
                                       1,
                                       pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
}

static bool aht20_is_busy(uint8_t status)
{
    return (status & AHT20_STATUS_BUSY_BIT) != 0;
}

static bool aht20_is_calibrated(uint8_t status)
{
    return (status & AHT20_STATUS_CALIBRATED_MASK) == AHT20_STATUS_CALIBRATED_MASK;
}

static uint8_t aht20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    if (data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x80) != 0) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static esp_err_t aht20_wait_ready(uint32_t timeout_ms)
{
    uint8_t status = 0;
    uint32_t elapsed_ms = 0;
    esp_err_t ret = ESP_FAIL;

    do {
        ret = aht20_read_status(&status);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!aht20_is_busy(status)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(AHT20_MEASURE_POLL_DELAY_MS));
        elapsed_ms += AHT20_MEASURE_POLL_DELAY_MS;
    } while (elapsed_ms < timeout_ms);

    return ESP_ERR_TIMEOUT;
}

static esp_err_t aht20_soft_reset(void)
{
    const uint8_t reset_cmd = 0xBA;
    esp_err_t ret = aht20_write_command(&reset_cmd, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_RESET_DELAY_MS));
    return ESP_OK;
}

static esp_err_t aht20_run_initialization(void)
{
    const uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
    uint8_t status = 0;
    esp_err_t ret = aht20_write_command(init_cmd, sizeof(init_cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_INIT_DELAY_MS));

    ret = aht20_wait_ready(AHT20_MEASURE_MAX_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = aht20_read_status(&status);
    if (ret != ESP_OK) {
        return ret;
    }

    return aht20_is_calibrated(status) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t aht20_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(AHT20_POWER_ON_DELAY_MS));

    uint8_t status = 0;
    esp_err_t ret = ESP_FAIL;

    for (int retry = 0; retry < AHT20_RETRY_COUNT; retry++) {
        ret = aht20_read_status(&status);
        if (ret != ESP_OK) {
            (void)aht20_soft_reset();
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        if (aht20_is_busy(status)) {
            ret = aht20_wait_ready(AHT20_MEASURE_MAX_WAIT_MS);
            if (ret != ESP_OK) {
                (void)aht20_soft_reset();
                vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
                continue;
            }
            ret = aht20_read_status(&status);
            if (ret != ESP_OK) {
                (void)aht20_soft_reset();
                vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
                continue;
            }
        }

        if (aht20_is_calibrated(status)) {
            return ESP_OK;
        }

        ret = aht20_run_initialization();
        if (ret == ESP_OK) {
            return ESP_OK;
        }

        (void)aht20_soft_reset();
        vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
    }

    return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
}

esp_err_t aht20_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    uint8_t data[AHT20_MEASURE_DATA_LEN] = {0};

    for (int retry = 0; retry < AHT20_RETRY_COUNT; retry++) {
        const uint8_t measure_cmd[3] = {0xAC, 0x33, 0x00};
        ret = aht20_write_command(measure_cmd, sizeof(measure_cmd));
        if (ret != ESP_OK) {
            (void)aht20_soft_reset();
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        ret = aht20_wait_ready(AHT20_MEASURE_MAX_WAIT_MS);
        if (ret != ESP_OK) {
            (void)aht20_soft_reset();
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        ret = i2c_master_read_from_device(BOARD_I2C_PORT,
                                          BOARD_AHT20_I2C_ADDR,
                                          data,
                                          sizeof(data),
                                          pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
        if (ret != ESP_OK) {
            (void)aht20_soft_reset();
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        if (aht20_is_busy(data[0])) {
            ret = ESP_ERR_INVALID_STATE;
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        if (aht20_crc8(data, sizeof(data) - 1) != data[sizeof(data) - 1]) {
            ret = ESP_ERR_INVALID_CRC;
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        if (!aht20_is_calibrated(data[0])) {
            ret = aht20_run_initialization();
            if (ret != ESP_OK) {
                (void)aht20_soft_reset();
                vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
                continue;
            }
        }

        if (ret == ESP_OK) {
            break;
        }
    }

    if (ret != ESP_OK) {
        return ret;
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
