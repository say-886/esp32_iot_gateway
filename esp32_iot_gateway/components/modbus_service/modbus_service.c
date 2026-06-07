#include "modbus_service.h"

#include <string.h>

#include "board.h"
#include "device_status.h"
#include "driver/uart.h"
#include "error_code.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage_nvs.h"

#define MODBUS_FUNCTION_READ_HOLDING 0x03U
#define MODBUS_RESPONSE_TIMEOUT_MS 500U
#define MODBUS_POLL_TASK_STACK_SIZE 4096U
#define MODBUS_POLL_TASK_PRIORITY 4U

static const char *TAG = "modbus_service";
static SemaphoreHandle_t s_bus_mutex;
static SemaphoreHandle_t s_status_mutex;
static TaskHandle_t s_poll_task;
static modbus_service_status_t s_status;
static app_config_t s_config;

static uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xA001U : crc >> 1U;
        }
    }
    return crc;
}

static void update_poll_status(esp_err_t err, const uint16_t *registers)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.last_error = err;
    if (err == ESP_OK) {
        memcpy(s_status.registers, registers, s_status.register_count * sizeof(registers[0]));
        s_status.online = true;
        s_status.success_count++;
        s_status.consecutive_failures = 0;
    } else {
        s_status.online = false;
        s_status.error_count++;
        s_status.consecutive_failures++;
    }
    uint32_t consecutive_failures = s_status.consecutive_failures;
    xSemaphoreGive(s_status_mutex);

    if (consecutive_failures >= 3U) {
        device_status_set_error(APP_ERR_MODBUS_READ_FAILED);
    } else if (err == ESP_OK) {
        device_status_clear_error(APP_ERR_MODBUS_READ_FAILED);
    }
}

esp_err_t modbus_service_read_holding_registers(uint8_t slave_addr,
                                                uint16_t start_register,
                                                uint16_t register_count,
                                                uint16_t *registers)
{
    if (s_bus_mutex == NULL || registers == NULL || slave_addr == 0U ||
        slave_addr > 247U || register_count == 0U ||
        register_count > MODBUS_SERVICE_MAX_REGISTERS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t request[8] = {
        slave_addr,
        MODBUS_FUNCTION_READ_HOLDING,
        (uint8_t)(start_register >> 8U),
        (uint8_t)start_register,
        (uint8_t)(register_count >> 8U),
        (uint8_t)register_count,
        0,
        0,
    };
    uint16_t request_crc = modbus_crc16(request, 6);
    request[6] = (uint8_t)request_crc;
    request[7] = (uint8_t)(request_crc >> 8U);

    uint8_t response[5U + MODBUS_SERVICE_MAX_REGISTERS * 2U] = {0};
    size_t expected_length = 5U + register_count * 2U;

    xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
    uart_flush_input(BOARD_RS485_UART);
    int written = uart_write_bytes(BOARD_RS485_UART, request, sizeof(request));
    if (written != sizeof(request)) {
        xSemaphoreGive(s_bus_mutex);
        return ESP_FAIL;
    }
    esp_err_t err = uart_wait_tx_done(BOARD_RS485_UART, pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS));
    if (err != ESP_OK) {
        xSemaphoreGive(s_bus_mutex);
        return err;
    }
    int received = uart_read_bytes(BOARD_RS485_UART,
                                   response,
                                   expected_length,
                                   pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS));
    xSemaphoreGive(s_bus_mutex);

    if (received == 5 && response[0] == slave_addr &&
        response[1] == (MODBUS_FUNCTION_READ_HOLDING | 0x80U)) {
        uint16_t exception_crc = (uint16_t)response[3] | ((uint16_t)response[4] << 8U);
        return modbus_crc16(response, 3) == exception_crc ? ESP_ERR_INVALID_RESPONSE
                                                          : ESP_ERR_INVALID_CRC;
    }
    if (received != (int)expected_length ||
        response[0] != slave_addr ||
        response[1] != MODBUS_FUNCTION_READ_HOLDING ||
        response[2] != register_count * 2U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t response_crc = (uint16_t)response[expected_length - 2U] |
                            ((uint16_t)response[expected_length - 1U] << 8U);
    if (modbus_crc16(response, expected_length - 2U) != response_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    for (uint16_t i = 0; i < register_count; ++i) {
        registers[i] = ((uint16_t)response[3U + i * 2U] << 8U) |
                       response[4U + i * 2U];
    }
    return ESP_OK;
}

static void modbus_poll_task(void *arg)
{
    (void)arg;
    uint16_t registers[MODBUS_SERVICE_MAX_REGISTERS] = {0};

    while (true) {
        esp_err_t err = modbus_service_read_holding_registers(s_config.modbus_slave_addr,
                                                              s_config.modbus_start_register,
                                                              s_config.modbus_register_count,
                                                              registers);
        update_poll_status(err, registers);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "poll failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(s_config.modbus_poll_period_ms));
    }
}

esp_err_t modbus_service_init(void)
{
    if (s_poll_task != NULL) {
        return ESP_OK;
    }

    esp_err_t err = storage_load_config(&s_config);
    if (err != ESP_OK) {
        return err;
    }

    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutex();
    }
    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutex();
    }
    if (s_status_mutex == NULL || s_bus_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = s_config.modbus_enabled;
    s_status.slave_addr = s_config.modbus_slave_addr;
    s_status.start_register = s_config.modbus_start_register;
    s_status.register_count = s_config.modbus_register_count;
    if (!s_config.modbus_enabled) {
        ESP_LOGI(TAG, "Modbus polling disabled by configuration");
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = (int)s_config.modbus_baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(BOARD_RS485_UART, 256, 0, 0, NULL, 0);
    if (err == ESP_OK) {
        err = uart_param_config(BOARD_RS485_UART, &uart_config);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(BOARD_RS485_UART,
                           BOARD_RS485_TX_GPIO,
                           BOARD_RS485_RX_GPIO,
                           BOARD_RS485_RTS_GPIO,
                           UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_set_mode(BOARD_RS485_UART, UART_MODE_RS485_HALF_DUPLEX);
    }
    if (err != ESP_OK) {
        uart_driver_delete(BOARD_RS485_UART);
        return err;
    }

    if (xTaskCreate(modbus_poll_task,
                    "modbus_poll",
                    MODBUS_POLL_TASK_STACK_SIZE,
                    NULL,
                    MODBUS_POLL_TASK_PRIORITY,
                    &s_poll_task) != pdPASS) {
        uart_driver_delete(BOARD_RS485_UART);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Modbus RTU polling enabled: slave=%u start=%u count=%u baud=%lu",
             s_config.modbus_slave_addr,
             s_config.modbus_start_register,
             s_config.modbus_register_count,
             (unsigned long)s_config.modbus_baud_rate);
    return ESP_OK;
}

void modbus_service_get_status(modbus_service_status_t *status)
{
    if (status == NULL) {
        return;
    }
    if (s_status_mutex == NULL) {
        memset(status, 0, sizeof(*status));
        return;
    }
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_status_mutex);
}
