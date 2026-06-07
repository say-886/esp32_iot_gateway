#ifndef SENSOR_AHT20_H
#define SENSOR_AHT20_H

#include "esp_err.h"

esp_err_t aht20_init(void);
esp_err_t aht20_read(float *temperature, float *humidity);

#endif
