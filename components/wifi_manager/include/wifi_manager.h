#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
bool wifi_manager_is_connected(void);

#endif
