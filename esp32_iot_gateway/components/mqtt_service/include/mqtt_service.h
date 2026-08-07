#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "device_status.h"
#include "esp_err.h"

/** @brief MQTT 可靠传输、离线队列和边缘计算指标。 */
typedef struct {
    bool connected;                /**< MQTT 会话是否在线。 */
    uint32_t offline_queued;        /**< Flash 中待补传记录数。 */
    uint32_t offline_capacity;      /**< Flash 队列理论容量。 */
    uint32_t offline_dropped;       /**< 已丢弃的新样本数。 */
    uint32_t offline_corrupted;     /**< CRC 损坏记录数。 */
    uint32_t outbox_bytes;          /**< ESP-MQTT 内存 outbox 占用。 */
    float edge_temperature_ema;     /**< 本地平滑温度。 */
    float edge_humidity_ema;        /**< 本地平滑湿度。 */
    float edge_light_ema;           /**< 本地平滑光照。 */
    uint32_t edge_anomaly_flags;    /**< 最近一次边缘异常位图。 */
    uint32_t edge_sample_count;     /**< 已处理的边缘样本数。 */
} mqtt_service_metrics_t;

/** @brief 加载配置并启动 MQTT 客户端、重连定时器和补传任务。 */
esp_err_t mqtt_service_start(void);
/** @brief 停止并销毁 MQTT 客户端，补传任务保留为空闲状态以支持后续重启。 */
esp_err_t mqtt_service_stop(void);
/** @brief 发布完整设备状态，QoS 1 且 retain。 */
esp_err_t mqtt_service_publish_status(const device_status_t *status);
/** @brief 兼容接口：将传感器数据写入可靠离线队列。 */
esp_err_t mqtt_service_publish_sensor(const device_status_t *status);
/** @brief 执行边缘计算并将遥测写入 Flash，联网与否均可调用。 */
esp_err_t mqtt_service_queue_sensor(const device_status_t *status);
/** @brief 发布设备心跳和队列流量指标。 */
esp_err_t mqtt_service_publish_heartbeat(const device_status_t *status);
/** @brief 发布设备错误状态。 */
esp_err_t mqtt_service_publish_error(const device_status_t *status);
/** @brief 获取 MQTT、离线队列与边缘计算指标快照。 */
void mqtt_service_get_metrics(mqtt_service_metrics_t *metrics);

#endif
