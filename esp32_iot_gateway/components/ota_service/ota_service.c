#include "ota_service.h"

#include <string.h>

#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_service";

esp_err_t ota_service_start_http_upgrade(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ESP_LOGI(TAG, "starting OTA from %s", url);
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t new_app_info = {0};
    err = esp_https_ota_get_img_desc(handle, &new_app_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA image descriptor failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err;
    }

    const esp_app_desc_t *running_app_info = esp_app_get_description();
    ESP_LOGI(TAG, "OTA version: running=%s new=%s",
             running_app_info->version,
             new_app_info.version);
    if (strncmp(new_app_info.version, running_app_info->version, sizeof(new_app_info.version)) == 0) {
        ESP_LOGE(TAG, "rejecting OTA image with the same version");
        esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_VERSION;
    }

    do {
        err = esp_https_ota_perform(handle);
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "OTA download failed or incomplete: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA succeeded, restarting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_service_confirm_running_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "running OTA image confirmed valid");
    } else {
        ESP_LOGE(TAG, "failed to confirm running OTA image: %s", esp_err_to_name(err));
    }
    return err;
}
