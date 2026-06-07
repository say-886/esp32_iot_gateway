#ifndef SENSOR_BH1750_H
#define SENSOR_BH1750_H

#include "esp_err.h"

esp_err_t bh1750_init(void);
esp_err_t bh1750_read(float *light_lux);

#endif
