#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/**
 * @brief 启动内嵌 HTTP 服务器并注册接口路由。
 */
esp_err_t web_server_start(void);

/**
 * @brief 在服务器已运行时停止内嵌 HTTP 服务。
 */
esp_err_t web_server_stop(void);

#endif
