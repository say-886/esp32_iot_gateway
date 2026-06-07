#ifndef APP_LOG_H
#define APP_LOG_H

#include "esp_log.h"

#define APP_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define APP_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define APP_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)

#endif
