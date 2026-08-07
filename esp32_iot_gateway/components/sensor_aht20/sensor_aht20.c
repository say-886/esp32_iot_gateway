#include "sensor_aht20.h"

#include <stdint.h>

#include "board.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AHT20_TIMEOUT_MS 1000       // 1000ms 读写超时时间
#define AHT20_POWER_ON_DELAY_MS 80
#define AHT20_RESET_DELAY_MS 20
#define AHT20_INIT_DELAY_MS 20
#define AHT20_MEASURE_MAX_WAIT_MS 200
#define AHT20_MEASURE_POLL_DELAY_MS 10 // 测量轮询延迟时间（ms）
#define AHT20_RETRY_COUNT 3           // 重试次数
#define AHT20_RETRY_DELAY_MS 20       // 重试延迟时间（ms）
#define AHT20_STATUS_BUSY_BIT 0x80    // 忙标志位
#define AHT20_STATUS_CALIBRATED_MASK 0x18 // 校准状态掩码
#define AHT20_MEASURE_DATA_LEN 7      // 测量数据长度（字节）

/**
 * @brief 向 AHT20 写入 I2C 命令。
 * 
 * @param command 命令缓冲区。
 * @param command_len 命令长度。
 * @return esp_err_t ESP_OK 成功。
 */
static esp_err_t aht20_write_command(const uint8_t *command, size_t command_len)        
{
    if (command == NULL || command_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_write_to_device(BOARD_I2C_PORT,                           // I2C 通道
                                     BOARD_AHT20_I2C_ADDR,             // AHT20 I2C 地址
                                     command,                              // 命令缓冲区
                                     command_len,                       // 命令长度
                                     pdMS_TO_TICKS(AHT20_TIMEOUT_MS)); // 超时时间

    board_i2c_unlock();
    return ret;
}

/**
 * @brief 读取 AHT20 的状态字节。
 * 
 * @param status 输出参数，存储读到的状态。
 * @return esp_err_t ESP_OK 成功。
 */
static esp_err_t aht20_read_status(uint8_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = board_i2c_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_read_from_device(BOARD_I2C_PORT,
                                      BOARD_AHT20_I2C_ADDR,
                                      status,
                                      1,
                                      pdMS_TO_TICKS(AHT20_TIMEOUT_MS));

    board_i2c_unlock();
    return ret;
}

/**
 * @brief 检查状态字节中的忙标志位。
 * 
 * @param status 状态字节。
 * @return true 传感器正忙。
 * @return false 传感器空闲。
 */
static bool aht20_is_busy(uint8_t status)
{
    return (status & AHT20_STATUS_BUSY_BIT) != 0;
}

/**
 * @brief 检查状态字节中的校准标志位。
 * 
 * @param status 状态字节。
 * @return true 已校准。
 * @return false 未校准。
 */
static bool aht20_is_calibrated(uint8_t status)
{
    return (status & AHT20_STATUS_CALIBRATED_MASK) == AHT20_STATUS_CALIBRATED_MASK; 
}

/**
 * @brief 计算 AHT20 专用的 CRC8 校验值。
 * 
 * @param data 数据缓冲区。
 * @param len 数据长度。
 * @return uint8_t 计算出的 CRC8 值。
 */
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

/**
 * @brief 等待 AHT20 退出忙状态。
 * 
 * @param timeout_ms 最大等待时间（毫秒）。
 * @return esp_err_t ESP_OK 成功，ESP_ERR_TIMEOUT 超时。
 */
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

/**
 * @brief 执行 AHT20 软件复位。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
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

/**
 * @brief 执行 AHT20 初始化序列（发送 0xBE 命令并检查校准位）。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
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

/**
 * @brief 初始化 AHT20 传感器。
 * 
 * 包含上电延迟、忙状态检查、校准检查及必要的初始化命令发送。
 * 
 * @return esp_err_t ESP_OK 成功。
 */
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

        if (aht20_is_calibrated(status)) {  // 已校准，初始化成功
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

/**
 * @brief 读取 AHT20 的温湿度数据。
 * 
 * 包含触发测量、等待、读取原始数据、CRC 校验及物理量换算。
 * 
 * @param temperature 输出参数，存储摄氏度（°C）。
 * @param humidity 输出参数，存储相对湿度（%RH）。
 * @return esp_err_t ESP_OK 成功。
 */
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

        esp_err_t lock_ret = board_i2c_lock();
        if (lock_ret != ESP_OK) {
            ret = lock_ret;
            (void)aht20_soft_reset();
            vTaskDelay(pdMS_TO_TICKS(AHT20_RETRY_DELAY_MS));
            continue;
        }

        ret = i2c_master_read_from_device(BOARD_I2C_PORT,
                                          BOARD_AHT20_I2C_ADDR,
                                          data,
                                          sizeof(data),
                                          pdMS_TO_TICKS(AHT20_TIMEOUT_MS));

        board_i2c_unlock();
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
