#include "edge_compute.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* 默认规则兼顾演示可观察性与常见环境传感器量程，后续可扩展为 NVS 配置。 */
#define EDGE_EMA_ALPHA 0.25F
#define EDGE_TEMP_MIN_C 0.0F
#define EDGE_TEMP_MAX_C 45.0F
#define EDGE_HUMIDITY_MIN_PERCENT 10.0F
#define EDGE_HUMIDITY_MAX_PERCENT 90.0F
#define EDGE_LIGHT_MAX_LUX 5000.0F
#define EDGE_TEMP_STEP_C 5.0F
#define EDGE_HUMIDITY_STEP_PERCENT 20.0F
#define EDGE_LIGHT_STEP_LUX 1500.0F

static SemaphoreHandle_t s_mutex;
static edge_compute_result_t s_result;
static bool s_ready;

/**
 * @brief 根据本地规则生成异常位图。
 *
 * 突变检测以更新前的指数移动平均值为基线，可过滤单点噪声并保留快速变化事件。
 */
static uint32_t evaluate_anomalies(float temperature,
                                   float humidity,
                                   float light,
                                   const edge_compute_result_t *previous)
{
    uint32_t flags = 0U;
    if (temperature > EDGE_TEMP_MAX_C) {
        flags |= EDGE_ALERT_TEMP_HIGH;
    } else if (temperature < EDGE_TEMP_MIN_C) {
        flags |= EDGE_ALERT_TEMP_LOW;
    }
    if (humidity > EDGE_HUMIDITY_MAX_PERCENT) {
        flags |= EDGE_ALERT_HUMIDITY_HIGH;
    } else if (humidity < EDGE_HUMIDITY_MIN_PERCENT) {
        flags |= EDGE_ALERT_HUMIDITY_LOW;
    }
    if (light > EDGE_LIGHT_MAX_LUX) {
        flags |= EDGE_ALERT_LIGHT_HIGH;
    }
    if (previous->sample_count > 0U &&
        (fabsf(temperature - previous->temperature_ema) >= EDGE_TEMP_STEP_C ||
         fabsf(humidity - previous->humidity_ema) >= EDGE_HUMIDITY_STEP_PERCENT ||
         fabsf(light - previous->light_ema) >= EDGE_LIGHT_STEP_LUX)) {
        flags |= EDGE_ALERT_SENSOR_STEP;
    }
    return flags;
}

esp_err_t edge_compute_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_result, 0, sizeof(s_result));
    s_ready = true;
    return ESP_OK;
}

esp_err_t edge_compute_process(float temperature,
                               float humidity,
                               float light,
                               edge_compute_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        memset(result, 0, sizeof(*result));
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t flags = evaluate_anomalies(temperature, humidity, light, &s_result);
    if (s_result.sample_count == 0U) {
        s_result.temperature_ema = temperature;
        s_result.humidity_ema = humidity;
        s_result.light_ema = light;
    } else {
        s_result.temperature_ema += EDGE_EMA_ALPHA * (temperature - s_result.temperature_ema);
        s_result.humidity_ema += EDGE_EMA_ALPHA * (humidity - s_result.humidity_ema);
        s_result.light_ema += EDGE_EMA_ALPHA * (light - s_result.light_ema);
    }
    s_result.anomaly_flags = flags;
    if (s_result.sample_count < UINT32_MAX) {
        s_result.sample_count++;
    }
    *result = s_result;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void edge_compute_get_snapshot(edge_compute_result_t *result)
{
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *result = s_result;
    xSemaphoreGive(s_mutex);
}
