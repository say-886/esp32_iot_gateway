#ifndef OFFLINE_STORE_H
#define OFFLINE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief 持久化遥测记录。
 *
 * 记录同时保存原始传感器值、边缘计算结果和采样身份。MQTT QoS 1 收到 PUBACK
 * 前记录不会出队，因此设备掉电或网络中断后仍可继续补传。
 */
typedef struct {
    uint32_t boot_id;            /**< 本次启动随机标识。 */
    uint32_t sequence;           /**< 本次启动内单调递增序号。 */
    uint32_t uptime_sec;         /**< 采样时设备运行秒数。 */
    uint64_t timestamp_ms;       /**< 有效 Unix 毫秒时间；未校时为 0。 */
    float temperature;           /**< 原始温度。 */
    float humidity;              /**< 原始湿度。 */
    float light;                 /**< 原始光照。 */
    float temperature_ema;       /**< 温度指数移动平均。 */
    float humidity_ema;          /**< 湿度指数移动平均。 */
    float light_ema;             /**< 光照指数移动平均。 */
    uint32_t edge_anomaly_flags; /**< 本地边缘规则异常位图。 */
    uint32_t error_code;         /**< 采样时设备错误码。 */
    uint32_t flags;              /**< 记录属性位图，例如离线采集标记。 */
} offline_store_record_t;

/** @brief Flash 离线队列运行统计。 */
typedef struct {
    uint32_t queued;    /**< 当前待补传记录数。 */
    uint32_t capacity;  /**< 分区理论记录容量。 */
    uint32_t dropped;   /**< 队列满或扇区暂不可复用时丢弃的新记录数。 */
    uint32_t corrupted; /**< CRC 校验失败记录数。 */
    uint32_t meta_erase_count; /**< 本次启动以来元数据扇区擦除次数（RAM 统计）。 */
    uint32_t data_erase_count; /**< 本次启动以来数据扇区擦除次数（RAM 统计）。 */
    bool faulted;              /**< Flash 操作发生不可恢复错误。 */
    esp_err_t last_error;      /**< 最近一次 Flash 操作错误码。 */
} offline_store_stats_t;

/**
 * @brief 初始化离线队列并恢复最新有效元数据。
 *
 * @return ESP_OK 成功；ESP_ERR_NOT_FOUND 分区不存在；其他值表示 Flash 或内存错误。
 */
esp_err_t offline_store_init(void);

/**
 * @brief 将一条遥测追加到 Flash 队列。
 *
 * @param record 待保存记录。
 * @return ESP_OK 成功；ESP_ERR_NO_MEM 队列当前无法接收新记录。
 */
esp_err_t offline_store_append(const offline_store_record_t *record);

/**
 * @brief 读取队首记录但不删除。
 *
 * @param record 输出队首记录。
 * @return ESP_OK 成功；ESP_ERR_NOT_FOUND 队列为空；ESP_ERR_INVALID_CRC 记录损坏。
 */
esp_err_t offline_store_peek(offline_store_record_t *record);

/**
 * @brief 在云端 QoS 1 确认后删除队首记录。
 *
 * 队首移动默认每 8 次批量持久化；掉电前可调用 offline_store_flush()，
 * 否则最多会重复发送尚未提交删除的记录。
 *
 * @return ESP_OK 成功；ESP_ERR_NOT_FOUND 队列为空。
 */
esp_err_t offline_store_pop(void);

/**
 * @brief 立即持久化尚未提交的队首移动操作。
 *
 * @return ESP_OK 成功；其他值表示 Flash 写入失败。
 */
esp_err_t offline_store_flush(void);

/**
 * @brief 获取离线队列统计快照。
 *
 * @param stats 输出统计信息。
 */
void offline_store_get_stats(offline_store_stats_t *stats);

#endif
