#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include "device_status.h"
#include "esp_err.h"

esp_err_t oled_init(void);
void oled_clear(void);
void oled_show_status(const device_status_t *status);

#endif
