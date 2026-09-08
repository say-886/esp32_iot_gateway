#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/**
 * @brief 初始化 ESP-IDF Wi-Fi Station 栈及其事件处理器。
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 从存储中加载 Wi-Fi 凭据并启动 Station 模式。
 */
esp_err_t wifi_manager_start(void);

/** @brief 判断设备是否处于首次配网 SoftAP 模式。 */
bool wifi_manager_is_provisioning(void);

/** @brief 获取首次配网热点 SSID。 */
esp_err_t wifi_manager_get_provisioning_ssid(char *ssid, size_t ssid_size);

/** @brief 配置保存后异步切回 Station 模式并连接目标 Wi-Fi。 */
esp_err_t wifi_manager_apply_saved_config(void);

/**
 * @brief 查询当前 Wi-Fi Station 是否处于已连接状态。
 *
 * @return `true` 表示已连接，`false` 表示未连接。
 */
bool wifi_manager_is_connected(void);

#endif
