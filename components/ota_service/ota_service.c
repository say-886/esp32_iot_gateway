#include "ota_service.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

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
static TaskHandle_t s_health_confirmation_task;

#define OTA_HEALTH_WINDOW_MS 60000U
#define OTA_TIME_RETRY_MS 30000U
#define OTA_VALID_UNIX_TIME 1700000000LL

/**
 * @brief 启动 OTA 升级流程，从指定 URL 下载新的固件镜像并更新。
 *
 * 该函数会使用 ESP HTTPS OTA API 从指定 URL 下载新的固件镜像，并在下载完成后进行验证和安装。
 * 如果下载或安装过程中发生错误，会返回相应的错误码。
 *
 * @param url 固件镜像的 URL 地址，必须是 HTTPS 协议。
 * @return esp_err_t ESP_OK 成功，其他值表示错误。
 */
esp_err_t ota_service_start_http_upgrade(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "OTA rejected: HTTPS URL required");
        return ESP_ERR_INVALID_ARG;
    }
    if (time(NULL) < OTA_VALID_UNIX_TIME) {
        ESP_LOGE(TAG, "OTA rejected: trusted system time is unavailable");
        return ESP_ERR_INVALID_STATE;
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
/**
 * @brief 确认当前运行的 OTA 镜像有效，取消回滚标记。
 *
 * 该函数会检查当前 OTA 镜像的状态，如果处于待验证状态，则调用 API 确认其有效性。
 * 如果镜像已经被确认或不处于待验证状态，则直接返回成功。
 *
 * @return esp_err_t ESP_OK 成功或无需确认，其他值表示错误。
 */
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

static void ota_health_confirmation_task(void *arg)
{
    bool require_time_sync = (bool)(uintptr_t)arg;
    ESP_LOGI(TAG,
             "OTA image pending verification; starting %u-second health window",
             (unsigned int)(OTA_HEALTH_WINDOW_MS / 1000U));
    vTaskDelay(pdMS_TO_TICKS(OTA_HEALTH_WINDOW_MS));

    while (require_time_sync && time(NULL) < OTA_VALID_UNIX_TIME) {
        ESP_LOGW(TAG, "OTA confirmation deferred: trusted time is still unavailable");
        vTaskDelay(pdMS_TO_TICKS(OTA_TIME_RETRY_MS));
    }

    esp_err_t err = ota_service_confirm_running_image();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "delayed OTA confirmation failed: %s", esp_err_to_name(err));
    }
    s_health_confirmation_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_service_schedule_health_confirmation(bool require_time_sync)
{
    if (s_health_confirmation_task != NULL) {
        return ESP_OK;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_FOUND || (err == ESP_OK && state != ESP_OTA_IMG_PENDING_VERIFY)) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(ota_health_confirmation_task,
                    "ota_health",
                    3072,
                    (void *)(uintptr_t)require_time_sync,
                    3,
                    &s_health_confirmation_task) != pdPASS) {
        s_health_confirmation_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
