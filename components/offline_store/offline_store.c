#include "offline_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * 分区前两个 4 KiB 扇区保存追加式元数据日志，剩余扇区保存定长记录。
 * 双元数据扇区轮换可避免每次入队/出队都擦除同一个扇区，并通过 CRC 在重启时
 * 选择最新有效版本。
 */
#define OFFLINE_PARTITION_LABEL "telemetry"
#define OFFLINE_PARTITION_SUBTYPE 0x40
#define OFFLINE_SECTOR_SIZE 4096U
#define OFFLINE_META_SECTORS 2U
#define OFFLINE_META_MAGIC 0x4F464D54U
#define OFFLINE_RECORD_MAGIC 0x4F465243U
#define OFFLINE_META_VERSION 2U
#define OFFLINE_DROPPED_PERSIST_INTERVAL 64U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t generation;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped;
    uint32_t corrupted;
    uint32_t crc32;
} offline_meta_entry_t;

typedef struct {
    uint32_t magic;
    offline_store_record_t record;
    uint32_t crc32;
} offline_flash_record_t;

static const char *TAG = "offline_store";
static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static offline_meta_entry_t s_meta;
static uint32_t s_meta_sector;
static uint32_t s_meta_slot;
static uint32_t s_records_per_sector;
static uint32_t s_slot_count;
static uint32_t s_capacity;
static bool s_ready;

/** @brief 使用标准 CRC32 多项式计算元数据和记录校验值。 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static uint32_t meta_crc(const offline_meta_entry_t *entry)
{
    return crc32_update(0U, (const uint8_t *)entry, offsetof(offline_meta_entry_t, crc32));
}

static uint32_t record_crc(const offline_flash_record_t *record)
{
    return crc32_update(0U, (const uint8_t *)record, offsetof(offline_flash_record_t, crc32));
}

static bool buffer_is_erased(const uint8_t *buffer, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (buffer[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

static bool meta_is_valid(const offline_meta_entry_t *entry)
{
    return entry->magic == OFFLINE_META_MAGIC &&
           entry->version == OFFLINE_META_VERSION &&
           entry->head < s_slot_count &&
           entry->tail < s_slot_count &&
           entry->count <= s_capacity &&
           entry->crc32 == meta_crc(entry);
}

static size_t meta_offset(uint32_t sector, uint32_t slot)
{
    return (size_t)sector * OFFLINE_SECTOR_SIZE +
           (size_t)slot * sizeof(offline_meta_entry_t);
}

static size_t data_offset(uint32_t index)
{
    uint32_t sector = index / s_records_per_sector;
    uint32_t slot = index % s_records_per_sector;
    return (size_t)(OFFLINE_META_SECTORS + sector) * OFFLINE_SECTOR_SIZE +
           (size_t)slot * sizeof(offline_flash_record_t);
}

static esp_err_t load_latest_meta(void)
{
    const uint32_t slots_per_sector = OFFLINE_SECTOR_SIZE / sizeof(offline_meta_entry_t);
    offline_meta_entry_t best = {0};
    bool found = false;
    uint32_t best_sector = 0;
    uint32_t best_slot = 0;

    for (uint32_t sector = 0; sector < OFFLINE_META_SECTORS; ++sector) {
        for (uint32_t slot = 0; slot < slots_per_sector; ++slot) {
            offline_meta_entry_t entry;
            esp_err_t err = esp_partition_read(s_partition,
                                               meta_offset(sector, slot),
                                               &entry,
                                               sizeof(entry));
            if (err != ESP_OK) {
                return err;
            }
            if (buffer_is_erased((const uint8_t *)&entry, sizeof(entry))) {
                break;
            }
            if (meta_is_valid(&entry) && (!found || entry.generation > best.generation)) {
                best = entry;
                best_sector = sector;
                best_slot = slot;
                found = true;
            }
        }
    }

    if (!found) {
        memset(&s_meta, 0, sizeof(s_meta));
        s_meta.magic = OFFLINE_META_MAGIC;
        s_meta.version = OFFLINE_META_VERSION;
        s_meta_sector = 0;
        s_meta_slot = 0;
        return ESP_ERR_NOT_FOUND;
    }

    s_meta = best;
    s_meta_sector = best_sector;
    s_meta_slot = best_slot;
    return ESP_OK;
}

static esp_err_t persist_meta_locked(void)
{
    const uint32_t slots_per_sector = OFFLINE_SECTOR_SIZE / sizeof(offline_meta_entry_t);
    uint32_t target_sector = s_meta_sector;
    uint32_t target_slot = s_meta_slot + 1U;

    if (target_slot >= slots_per_sector) {
        target_sector = (s_meta_sector + 1U) % OFFLINE_META_SECTORS;
        target_slot = 0;
        esp_err_t erase_err = esp_partition_erase_range(s_partition,
                                                        (size_t)target_sector * OFFLINE_SECTOR_SIZE,
                                                        OFFLINE_SECTOR_SIZE);
        if (erase_err != ESP_OK) {
            return erase_err;
        }
    }

    s_meta.generation++;
    s_meta.crc32 = meta_crc(&s_meta);
    esp_err_t err = esp_partition_write(s_partition,
                                        meta_offset(target_sector, target_slot),
                                        &s_meta,
                                        sizeof(s_meta));
    if (err == ESP_OK) {
        s_meta_sector = target_sector;
        s_meta_slot = target_slot;
    }
    return err;
}

esp_err_t offline_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           OFFLINE_PARTITION_SUBTYPE,
                                           OFFLINE_PARTITION_LABEL);
    if (s_partition == NULL || s_partition->size <= OFFLINE_META_SECTORS * OFFLINE_SECTOR_SIZE) {
        ESP_LOGE(TAG, "partition '%s' not found or too small", OFFLINE_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    s_records_per_sector = OFFLINE_SECTOR_SIZE / sizeof(offline_flash_record_t);
    uint32_t data_sectors = (s_partition->size / OFFLINE_SECTOR_SIZE) - OFFLINE_META_SECTORS;
    s_slot_count = data_sectors * s_records_per_sector;
    /*
     * 永久保留一个空数据扇区，使 head 回卷到新扇区时 tail 必然已离开该扇区。
     * 这样可以安全执行整扇区擦除，而不必等待或破坏仍未确认的队列记录。
     */
    s_capacity = data_sectors > 1U ? (data_sectors - 1U) * s_records_per_sector : 0U;
    if (s_records_per_sector == 0U || s_slot_count == 0U || s_capacity == 0U) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = load_latest_meta();
    if (err == ESP_ERR_NOT_FOUND) {
        err = esp_partition_erase_range(s_partition,
                                        0,
                                        OFFLINE_META_SECTORS * OFFLINE_SECTOR_SIZE);
        if (err == ESP_OK) {
            s_meta.crc32 = meta_crc(&s_meta);
            err = esp_partition_write(s_partition, 0, &s_meta, sizeof(s_meta));
            s_meta_sector = 0;
            s_meta_slot = 0;
        }
    }
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG,
             "offline queue ready: partition=%lu bytes record=%u capacity=%lu queued=%lu",
             (unsigned long)s_partition->size,
             (unsigned int)sizeof(offline_flash_record_t),
             (unsigned long)s_capacity,
             (unsigned long)s_meta.count);
    return ESP_OK;
}

esp_err_t offline_store_append(const offline_store_record_t *record)
{
    if (!s_ready || record == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_meta.count >= s_capacity) {
        s_meta.dropped++;
        esp_err_t meta_err = (s_meta.dropped % OFFLINE_DROPPED_PERSIST_INTERVAL) == 0U
                                 ? persist_meta_locked()
                                 : ESP_OK;
        xSemaphoreGive(s_mutex);
        return meta_err == ESP_OK ? ESP_ERR_NO_MEM : meta_err;
    }

    uint32_t index = s_meta.head;
    if ((index % s_records_per_sector) == 0U) {
        /*
         * Flash 只能按扇区擦除。环形队列回卷后，如果 tail 仍位于当前
         * head 扇区，说明该扇区内还有未确认数据；此时宁可丢弃本次新样本，
         * 也不能擦除整扇区破坏仍待补传的旧记录。
         */
        uint32_t head_sector = index / s_records_per_sector;
        uint32_t tail_sector = s_meta.tail / s_records_per_sector;
        if (s_meta.count > 0U && head_sector == tail_sector) {
            s_meta.dropped++;
            esp_err_t meta_err = (s_meta.dropped % OFFLINE_DROPPED_PERSIST_INTERVAL) == 0U
                                     ? persist_meta_locked()
                                     : ESP_OK;
            xSemaphoreGive(s_mutex);
            return meta_err == ESP_OK ? ESP_ERR_NO_MEM : meta_err;
        }
        size_t sector_offset = data_offset(index);
        esp_err_t erase_err = esp_partition_erase_range(s_partition,
                                                        sector_offset,
                                                        OFFLINE_SECTOR_SIZE);
        if (erase_err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return erase_err;
        }
    }

    offline_flash_record_t flash_record = {
        .magic = OFFLINE_RECORD_MAGIC,
        .record = *record,
    };
    flash_record.crc32 = record_crc(&flash_record);
    esp_err_t err = esp_partition_write(s_partition,
                                        data_offset(index),
                                        &flash_record,
                                        sizeof(flash_record));
    if (err == ESP_OK) {
        s_meta.head = (s_meta.head + 1U) % s_slot_count;
        s_meta.count++;
        err = persist_meta_locked();
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t offline_store_peek(offline_store_record_t *record)
{
    if (!s_ready || record == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_meta.count == 0U) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    offline_flash_record_t flash_record;
    esp_err_t err = esp_partition_read(s_partition,
                                       data_offset(s_meta.tail),
                                       &flash_record,
                                       sizeof(flash_record));
    if (err == ESP_OK &&
        (flash_record.magic != OFFLINE_RECORD_MAGIC || flash_record.crc32 != record_crc(&flash_record))) {
        s_meta.corrupted++;
        persist_meta_locked();
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) {
        *record = flash_record.record;
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t offline_store_pop(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_meta.count == 0U) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    s_meta.tail = (s_meta.tail + 1U) % s_slot_count;
    s_meta.count--;
    esp_err_t err = persist_meta_locked();
    xSemaphoreGive(s_mutex);
    return err;
}

void offline_store_get_stats(offline_store_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    stats->queued = s_meta.count;
    stats->capacity = s_capacity;
    stats->dropped = s_meta.dropped;
    stats->corrupted = s_meta.corrupted;
    xSemaphoreGive(s_mutex);
}
