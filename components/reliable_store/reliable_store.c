#include "reliable_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define RELIABLE_PARTITION_LABEL "reliable_telemetry"
#define RELIABLE_PARTITION_SUBTYPE 0x41
#define RELIABLE_SECTOR_SIZE 4096U
#define RELIABLE_META_SECTORS 8U
#define RELIABLE_META_MAGIC 0x524D4554U
#define RELIABLE_RECORD_MAGIC 0x52524543U
#define RELIABLE_META_VERSION 1U
#define RELIABLE_COMMIT_MARKER 0x00000000U
#ifndef RELIABLE_META_POP_COMMIT_INTERVAL
#define RELIABLE_META_POP_COMMIT_INTERVAL 8U
#endif

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
} reliable_meta_entry_t;

/* committed is written last, so a power cut during a record write never makes it valid. */
typedef struct {
    uint32_t magic;
    reliable_store_record_t record;
    uint32_t crc32;
    uint32_t committed;
} reliable_flash_record_t;

static const char *TAG = "reliable_store";
static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static reliable_meta_entry_t s_meta;
static uint32_t s_meta_sector;
static uint32_t s_meta_slot;
static uint32_t s_records_per_sector;
static uint32_t s_slot_count;
static uint32_t s_capacity;
static uint32_t s_pending_pop_count;
static uint32_t s_meta_erase_count;
static uint32_t s_data_erase_count;
static bool s_faulted;
static esp_err_t s_last_error;
static bool s_ready;

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

static uint32_t meta_crc(const reliable_meta_entry_t *entry)
{
    return crc32_update(0U, (const uint8_t *)entry, offsetof(reliable_meta_entry_t, crc32));
}

static uint32_t record_crc(const reliable_flash_record_t *record)
{
    return crc32_update(0U, (const uint8_t *)record, offsetof(reliable_flash_record_t, crc32));
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

static bool generation_is_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

static bool meta_is_valid(const reliable_meta_entry_t *entry)
{
    return entry->magic == RELIABLE_META_MAGIC && entry->version == RELIABLE_META_VERSION &&
           entry->head < s_slot_count && entry->tail < s_slot_count &&
           entry->count <= s_capacity && entry->crc32 == meta_crc(entry);
}

static bool record_is_valid(const reliable_flash_record_t *record)
{
    return record->magic == RELIABLE_RECORD_MAGIC &&
           record->committed == RELIABLE_COMMIT_MARKER &&
           record->record.payload_len < RELIABLE_STORE_PAYLOAD_MAX_LEN &&
           record->record.event_type[RELIABLE_STORE_TYPE_MAX_LEN - 1U] == '\0' &&
           record->record.payload[record->record.payload_len] == '\0' &&
           record->crc32 == record_crc(record);
}

static esp_err_t set_fault(esp_err_t err)
{
    if (err != ESP_OK) {
        s_faulted = true;
        s_last_error = err;
    }
    return err;
}

static size_t meta_offset(uint32_t sector, uint32_t slot)
{
    return (size_t)sector * RELIABLE_SECTOR_SIZE +
           (size_t)slot * sizeof(reliable_meta_entry_t);
}

static size_t data_offset(uint32_t index)
{
    uint32_t sector = index / s_records_per_sector;
    uint32_t slot = index % s_records_per_sector;
    return (size_t)(RELIABLE_META_SECTORS + sector) * RELIABLE_SECTOR_SIZE +
           (size_t)slot * sizeof(reliable_flash_record_t);
}

static esp_err_t load_latest_meta(bool *all_erased)
{
    const uint32_t slots_per_sector = RELIABLE_SECTOR_SIZE / sizeof(reliable_meta_entry_t);
    reliable_meta_entry_t best = {0};
    bool found = false;
    *all_erased = true;

    for (uint32_t sector = 0; sector < RELIABLE_META_SECTORS; ++sector) {
        for (uint32_t slot = 0; slot < slots_per_sector; ++slot) {
            reliable_meta_entry_t entry;
            esp_err_t err = esp_partition_read(s_partition, meta_offset(sector, slot),
                                               &entry, sizeof(entry));
            if (err != ESP_OK) {
                return set_fault(err);
            }
            if (buffer_is_erased((const uint8_t *)&entry, sizeof(entry))) {
                break;
            }
            *all_erased = false;
            if (meta_is_valid(&entry) &&
                (!found || generation_is_newer(entry.generation, best.generation))) {
                best = entry;
                s_meta_sector = sector;
                s_meta_slot = slot;
                found = true;
            }
        }
    }
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    s_meta = best;
    return ESP_OK;
}

static esp_err_t persist_meta_locked(void)
{
    const uint32_t slots_per_sector = RELIABLE_SECTOR_SIZE / sizeof(reliable_meta_entry_t);
    uint32_t sector = s_meta_sector;
    uint32_t slot = s_meta_slot + 1U;
    if (slot >= slots_per_sector) {
        sector = (s_meta_sector + 1U) % RELIABLE_META_SECTORS;
        slot = 0U;
        esp_err_t err = esp_partition_erase_range(s_partition,
                                                  (size_t)sector * RELIABLE_SECTOR_SIZE,
                                                  RELIABLE_SECTOR_SIZE);
        if (err != ESP_OK) {
            return set_fault(err);
        }
        s_meta_erase_count++;
    }
    s_meta.generation++;
    s_meta.crc32 = meta_crc(&s_meta);
    esp_err_t err = esp_partition_write(s_partition, meta_offset(sector, slot),
                                        &s_meta, sizeof(s_meta));
    if (err == ESP_OK) {
        s_meta_sector = sector;
        s_meta_slot = slot;
        s_pending_pop_count = 0U;
    } else {
        set_fault(err);
    }
    return err;
}

static esp_err_t recover_orphan_records_locked(void)
{
    bool recovered = false;
    while (s_meta.count < s_capacity) {
        reliable_flash_record_t record;
        esp_err_t err = esp_partition_read(s_partition, data_offset(s_meta.head),
                                           &record, sizeof(record));
        if (err != ESP_OK) {
            return set_fault(err);
        }
        if (!record_is_valid(&record)) {
            break;
        }
        s_meta.head = (s_meta.head + 1U) % s_slot_count;
        s_meta.count++;
        recovered = true;
    }
    if (recovered) {
        ESP_LOGW(TAG, "recovered %lu committed records missing from metadata",
                 (unsigned long)s_meta.count);
        return persist_meta_locked();
    }
    return ESP_OK;
}

esp_err_t reliable_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           RELIABLE_PARTITION_SUBTYPE,
                                           RELIABLE_PARTITION_LABEL);
    if (s_partition == NULL || s_partition->size <= RELIABLE_META_SECTORS * RELIABLE_SECTOR_SIZE) {
        return ESP_ERR_NOT_FOUND;
    }
    s_records_per_sector = RELIABLE_SECTOR_SIZE / sizeof(reliable_flash_record_t);
    uint32_t data_sectors = (s_partition->size / RELIABLE_SECTOR_SIZE) - RELIABLE_META_SECTORS;
    s_slot_count = data_sectors * s_records_per_sector;
    s_capacity = data_sectors > 1U ? (data_sectors - 1U) * s_records_per_sector : 0U;
    if (s_records_per_sector == 0U || s_capacity == 0U) {
        return ESP_ERR_INVALID_SIZE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool all_erased = false;
    esp_err_t err = load_latest_meta(&all_erased);
    if (err == ESP_ERR_NOT_FOUND && all_erased) {
        memset(&s_meta, 0, sizeof(s_meta));
        s_meta.magic = RELIABLE_META_MAGIC;
        s_meta.version = RELIABLE_META_VERSION;
        s_meta.crc32 = meta_crc(&s_meta);
        err = esp_partition_write(s_partition, 0, &s_meta, sizeof(s_meta));
        s_meta_sector = 0U;
        s_meta_slot = 0U;
    } else if (err == ESP_ERR_NOT_FOUND) {
        /* Existing but invalid metadata is a fault: never reset critical records silently. */
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) {
        err = recover_orphan_records_locked();
    }
    if (err != ESP_OK) {
        set_fault(err);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "reliable queue ready: capacity=%lu", (unsigned long)s_capacity);
    return ESP_OK;
}

esp_err_t reliable_store_append(const reliable_store_record_t *record)
{
    if (!s_ready || record == NULL || record->payload_len >= RELIABLE_STORE_PAYLOAD_MAX_LEN ||
        record->event_type[RELIABLE_STORE_TYPE_MAX_LEN - 1U] != '\0' ||
        record->payload[record->payload_len] != '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_faulted) {
        esp_err_t err = s_last_error;
        xSemaphoreGive(s_mutex);
        return err;
    }
    if (s_meta.count >= s_capacity) {
        s_meta.dropped++;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    uint32_t index = s_meta.head;
    if ((index % s_records_per_sector) == 0U) {
        uint32_t head_sector = index / s_records_per_sector;
        uint32_t tail_sector = s_meta.tail / s_records_per_sector;
        if (s_meta.count > 0U && head_sector == tail_sector) {
            s_meta.dropped++;
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NO_MEM;
        }
        esp_err_t err = esp_partition_erase_range(s_partition, data_offset(index),
                                                  RELIABLE_SECTOR_SIZE);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return set_fault(err);
        }
        s_data_erase_count++;
    }
    reliable_flash_record_t flash_record;
    memset(&flash_record, 0, sizeof(flash_record));
    flash_record.magic = RELIABLE_RECORD_MAGIC;
    flash_record.record = *record;
    flash_record.committed = UINT32_MAX;
    flash_record.crc32 = record_crc(&flash_record);
    esp_err_t err = esp_partition_write(s_partition, data_offset(index), &flash_record,
                                        offsetof(reliable_flash_record_t, committed));
    if (err == ESP_OK) {
        const uint32_t marker = RELIABLE_COMMIT_MARKER;
        err = esp_partition_write(s_partition,
                                  data_offset(index) + offsetof(reliable_flash_record_t, committed),
                                  &marker, sizeof(marker));
    }
    if (err == ESP_OK) {
        s_meta.head = (s_meta.head + 1U) % s_slot_count;
        s_meta.count++;
        err = persist_meta_locked();
    }
    if (err != ESP_OK) {
        set_fault(err);
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t reliable_store_peek(reliable_store_record_t *record)
{
    if (!s_ready || record == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_faulted) {
        esp_err_t err = s_last_error;
        xSemaphoreGive(s_mutex);
        return err;
    }
    if (s_meta.count == 0U) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    reliable_flash_record_t flash_record;
    esp_err_t err = esp_partition_read(s_partition, data_offset(s_meta.tail), &flash_record,
                                       sizeof(flash_record));
    if (err == ESP_OK && !record_is_valid(&flash_record)) {
        s_meta.corrupted++;
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) {
        *record = flash_record.record;
    } else {
        set_fault(err);
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t reliable_store_pop(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_faulted) {
        esp_err_t err = s_last_error;
        xSemaphoreGive(s_mutex);
        return err;
    }
    if (s_meta.count == 0U) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    s_meta.tail = (s_meta.tail + 1U) % s_slot_count;
    s_meta.count--;
    s_pending_pop_count++;
    esp_err_t err = s_pending_pop_count >= RELIABLE_META_POP_COMMIT_INTERVAL
                        ? persist_meta_locked() : ESP_OK;
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t reliable_store_flush(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = s_faulted ? s_last_error :
                    (s_pending_pop_count > 0U ? persist_meta_locked() : ESP_OK);
    xSemaphoreGive(s_mutex);
    return err;
}

void reliable_store_get_stats(reliable_store_stats_t *stats)
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
    stats->meta_erase_count = s_meta_erase_count;
    stats->data_erase_count = s_data_erase_count;
    stats->faulted = s_faulted;
    stats->last_error = s_last_error;
    xSemaphoreGive(s_mutex);
}
