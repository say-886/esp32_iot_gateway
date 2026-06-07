#ifndef WATCHDOG_SERVICE_H
#define WATCHDOG_SERVICE_H

#include "esp_err.h"

esp_err_t watchdog_service_init(void);
esp_err_t watchdog_service_register_current_task(void);
esp_err_t watchdog_service_feed(void);

#endif
