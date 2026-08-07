#ifndef EDGE_COMPUTE_H
#define EDGE_COMPUTE_H

#include <stdint.h>

#include "esp_err.h"

/** @brief 温度超过本地安全上限。 */
#define EDGE_ALERT_TEMP_HIGH (1U << 0)
/** @brief 温度低于本地安全下限。 */
#define EDGE_ALERT_TEMP_LOW (1U << 1)
/** @brief 湿度超过本地安全上限。 */
#define EDGE_ALERT_HUMIDITY_HIGH (1U << 2)
/** @brief 湿度低于本地安全下限。 */
#define EDGE_ALERT_HUMIDITY_LOW (1U << 3)
/** @brief 光照超过本地安全上限。 */
#define EDGE_ALERT_LIGHT_HIGH (1U << 4)
/** @brief 当前采样相对平滑值发生突变。 */
#define EDGE_ALERT_SENSOR_STEP (1U << 5)

/**
 * @brief 边缘计算结果。
 *
 * 结果由设备本地即时计算，不依赖云端可用性，可随离线遥测一并缓存和补传。
 */
typedef struct {
    float temperature_ema; /**< 温度指数移动平均值。 */
    float humidity_ema;    /**< 湿度指数移动平均值。 */
    float light_ema;       /**< 光照指数移动平均值。 */
    uint32_t anomaly_flags;/**< 本次采样触发的异常位图。 */
    uint32_t sample_count; /**< 本次启动以来参与计算的样本数。 */
} edge_compute_result_t;

/**
 * @brief 初始化本地边缘计算状态。
 *
 * @return ESP_OK 初始化成功；ESP_ERR_NO_MEM 互斥锁创建失败。
 */
esp_err_t edge_compute_init(void);

/**
 * @brief 对最新环境采样执行平滑、阈值判断和突变检测。
 *
 * @param temperature 当前温度，单位摄氏度。
 * @param humidity 当前相对湿度，单位百分比。
 * @param light 当前光照强度，单位 Lux。
 * @param result 输出本次边缘计算结果。
 * @return ESP_OK 计算成功；ESP_ERR_INVALID_ARG 参数为空；ESP_ERR_INVALID_STATE 未初始化。
 */
esp_err_t edge_compute_process(float temperature,
                               float humidity,
                               float light,
                               edge_compute_result_t *result);

/**
 * @brief 获取最近一次边缘计算快照。
 *
 * @param result 输出最近一次计算结果。
 */
void edge_compute_get_snapshot(edge_compute_result_t *result);

#endif
