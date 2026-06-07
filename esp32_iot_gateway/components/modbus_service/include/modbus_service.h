#ifndef MODBUS_SERVICE_H
#define MODBUS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define MODBUS_SERVICE_MAX_REGISTERS 16

typedef struct {
    bool enabled;
    bool online;
    uint8_t slave_addr;
    uint16_t start_register;
    uint16_t register_count;
    uint16_t registers[MODBUS_SERVICE_MAX_REGISTERS];
    uint32_t success_count;
    uint32_t error_count;
    uint32_t consecutive_failures;
    esp_err_t last_error;
} modbus_service_status_t;

esp_err_t modbus_service_init(void);
void modbus_service_get_status(modbus_service_status_t *status);
esp_err_t modbus_service_read_holding_registers(uint8_t slave_addr,
                                                uint16_t start_register,
                                                uint16_t register_count,
                                                uint16_t *registers);

#endif
