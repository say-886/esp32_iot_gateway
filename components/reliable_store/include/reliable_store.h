#ifndef RELIABLE_STORE_H
#define RELIABLE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define RELIABLE_STORE_TYPE_MAX_LEN 24U
#define RELIABLE_STORE_PAYLOAD_MAX_LEN 256U

/**
 * @brief A critical telemetry item that must survive normal resets and power loss
 * after reliable_store_append() returns successfully.
 */
typedef struct {
    uint32_t boot_id;
    uint32_t sequence;
    uint64_t timestamp_ms;
    char event_type[RELIABLE_STORE_TYPE_MAX_LEN];
    uint16_t payload_len;
    char payload[RELIABLE_STORE_PAYLOAD_MAX_LEN];
} reliable_store_record_t;

typedef struct {
    uint32_t queued;
    uint32_t capacity;
    uint32_t dropped;
    uint32_t corrupted;
    uint32_t meta_erase_count;
    uint32_t data_erase_count;
    bool faulted;
    esp_err_t last_error;
} reliable_store_stats_t;

esp_err_t reliable_store_init(void);
esp_err_t reliable_store_append(const reliable_store_record_t *record);
esp_err_t reliable_store_peek(reliable_store_record_t *record);
esp_err_t reliable_store_pop(void);
esp_err_t reliable_store_flush(void);
void reliable_store_get_stats(reliable_store_stats_t *stats);

#endif
