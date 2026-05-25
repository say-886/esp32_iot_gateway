#include "app_tasks.h"

#include "app_config.h"
#include "button.h"
#include "device_control.h"
#include "error_code.h"
#include "mqtt_service.h"
#include "oled_ssd1306.h"
#include "sensor_aht20.h"
#include "sensor_bh1750.h"
#include "storage_nvs.h"
#include "watchdog_service.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_tasks";
static const uint32_t DISPLAY_UPDATE_PERIOD_MS = 1000;
static const uint32_t STATUS_LOG_PERIOD_MS = 20000;
static const uint32_t MQTT_PUBLISH_PERIOD_MS = 5000;
static const uint32_t SENSOR_ERROR_THRESHOLD = 3;
static const uint32_t SENSOR_ERROR_LOG_PERIOD = 10;

static uint32_t app_get_sample_period_ms(void)
{
    app_config_t config;
    esp_err_t err = storage_load_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load config failed, use default sample period: %s", esp_err_to_name(err));
        return APP_DEFAULT_SAMPLE_PERIOD_MS;
    }

    if (config.sample_period_ms < 500 || config.sample_period_ms > 60000) {
        ESP_LOGW(TAG, "invalid sample period %lu ms, fallback to default",
                 (unsigned long)config.sample_period_ms);
        return APP_DEFAULT_SAMPLE_PERIOD_MS;
    }
    return config.sample_period_ms;
}

void app_status_init(device_status_t *status)
{
    device_status_init_default(status);
}

static void sensor_task(void *arg)
{
    (void)arg;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float light = 0.0f;
    device_status_t status;
    uint32_t fail_count = 0;
    bool last_read_ok = true;

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        uint32_t sample_period_ms = app_get_sample_period_ms();
        device_status_get(&status);
        temperature = status.temperature;
        humidity = status.humidity;
        light = status.light;

        esp_err_t aht_ret = aht20_read(&temperature, &humidity);
        esp_err_t bh_ret = bh1750_read(&light);

        if (aht_ret == ESP_OK && bh_ret == ESP_OK) {
            device_status_update_sensor(temperature, humidity, light);
            device_status_set_error(APP_ERR_NONE);
            if (!last_read_ok) {
                ESP_LOGI(TAG, "sensor recovered: temp=%.2f C hum=%.2f %%RH light=%.2f lux",
                         temperature, humidity, light);
            }
            fail_count = 0;
            last_read_ok = true;
        } else {
            if (aht_ret == ESP_OK || bh_ret == ESP_OK) {
                device_status_update_sensor(temperature, humidity, light);
            }
            fail_count++;

            if (fail_count >= SENSOR_ERROR_THRESHOLD) {
                device_status_set_error(aht_ret != ESP_OK ? APP_ERR_AHT20_READ_FAILED : APP_ERR_BH1750_READ_FAILED);
            }

            if (last_read_ok ||
                fail_count == SENSOR_ERROR_THRESHOLD ||
                (fail_count % SENSOR_ERROR_LOG_PERIOD) == 0U) {
                ESP_LOGW(TAG, "sensor read degraded: AHT20=%s BH1750=%s (consecutive_fail=%lu threshold=%lu)",
                         esp_err_to_name(aht_ret), esp_err_to_name(bh_ret),
                         (unsigned long)fail_count,
                         (unsigned long)SENSOR_ERROR_THRESHOLD);
            }
            last_read_ok = false;
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
    }
}

static void display_task(void *arg)
{
    (void)arg;
    device_status_t status;
    uint32_t publish_elapsed_ms = 0;
    uint32_t heartbeat_elapsed_ms = 0;
    uint32_t log_elapsed_ms = STATUS_LOG_PERIOD_MS;
    device_state_t last_state = DEVICE_STATE_INIT;
    uint32_t last_error = APP_ERR_NONE;

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_get(&status);
        oled_show_status(&status);

        if (log_elapsed_ms >= STATUS_LOG_PERIOD_MS ||
            status.state != last_state ||
            status.error_code != last_error) {
            ESP_LOGI(TAG,
                     "status: state=%s temp=%.2f hum=%.2f light=%.2f led=%d buzzer=%d relay=%d uptime=%lu err=%lu",
                     app_state_to_string(status.state),
                     status.temperature,
                     status.humidity,
                     status.light,
                     status.led_on,
                     status.buzzer_on,
                     status.relay_on,
                     (unsigned long)status.uptime_sec,
                     (unsigned long)status.error_code);
            log_elapsed_ms = 0;
            last_state = status.state;
            last_error = status.error_code;
        }

        if (status.mqtt_connected) {
            publish_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
            heartbeat_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
            if (publish_elapsed_ms >= MQTT_PUBLISH_PERIOD_MS) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_status(&status));
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_sensor(&status));
                if (status.error_code != APP_ERR_NONE) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_error(&status));
                }
                publish_elapsed_ms = 0;
            }
            if (heartbeat_elapsed_ms >= APP_DEFAULT_HEARTBEAT_PERIOD_MS) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_heartbeat(&status));
                heartbeat_elapsed_ms = 0;
            }
        } else {
            publish_elapsed_ms = 0;
            heartbeat_elapsed_ms = 0;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_PERIOD_MS));
        log_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
    }
}

static void update_single_output(bool led_set, bool led_on,
                                 bool buzzer_set, bool buzzer_on,
                                 bool relay_set, bool relay_on)
{
    device_cmd_t cmd = {
        .led_set = led_set,
        .led_value = led_on,
        .buzzer_set = buzzer_set,
        .buzzer_value = buzzer_on,
        .relay_set = relay_set,
        .relay_value = relay_on,
    };
    device_status_update_control(&cmd);
}

static void button_task(void *arg)
{
    (void)arg;
    device_status_t status;

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_get(&status);

        if (button_scan_key(BUTTON_ID_K1) == BUTTON_EVENT_SHORT_PRESS) {
            update_single_output(true, !status.led_on, false, false, false, false);
            ESP_LOGI(TAG, "K1 short press: LED toggle");
        }
        if (button_scan_key(BUTTON_ID_K2) == BUTTON_EVENT_SHORT_PRESS) {
            update_single_output(false, false, true, !status.buzzer_on, false, false);
            ESP_LOGI(TAG, "K2 short press: buzzer toggle");
        }
        if (button_scan_key(BUTTON_ID_K3) == BUTTON_EVENT_SHORT_PRESS) {
            update_single_output(false, false, false, false, true, !status.relay_on);
            ESP_LOGI(TAG, "K3 short press: relay toggle");
        }
        if (button_scan_key(BUTTON_ID_K4) == BUTTON_EVENT_SHORT_PRESS) {
            update_single_output(true, false, true, false, true, false);
            ESP_LOGI(TAG, "K4 short press: all outputs off");
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void control_task(void *arg)
{
    (void)arg;
    bool last_led = false;
    bool last_buzzer = false;
    bool last_relay = false;
    device_status_t status;

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_get(&status);
        if (status.led_on != last_led) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(device_led_set(status.led_on));
            last_led = status.led_on;
        }
        if (status.buzzer_on != last_buzzer) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(device_buzzer_set(status.buzzer_on));
            last_buzzer = status.buzzer_on;
        }
        if (status.relay_on != last_relay) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(device_relay_set(status.relay_on));
            last_relay = status.relay_on;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_tick(1);
        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_create_placeholder_tasks(void)
{
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 3072, NULL, 4, NULL);
    xTaskCreate(button_task, "button_task", 3072, NULL, 6, NULL);
    xTaskCreate(control_task, "control_task", 3072, NULL, 5, NULL);
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "hardware FreeRTOS tasks created");
}
