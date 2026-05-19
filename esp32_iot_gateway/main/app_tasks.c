#include "app_tasks.h"

#include "app_config.h"
#include "device_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_tasks";

void app_status_init(device_status_t *status)
{
    device_status_init_default(status);
}

static void sensor_task(void *arg)
{
    (void)arg;
    float temperature = 26.5f;
    float humidity = 60.2f;
    float light = 380.0f;

    while (true) {
        device_status_update_sensor(temperature, humidity, light);
        ESP_LOGI(TAG, "fake sensor: temp=%.1f hum=%.1f light=%.1f",
                 temperature, humidity, light);
        temperature += 0.1f;
        humidity += 0.1f;
        light += 1.0f;
        vTaskDelay(pdMS_TO_TICKS(APP_DEFAULT_SAMPLE_PERIOD_MS));
    }
}

static void display_task(void *arg)
{
    (void)arg;
    device_status_t status;

    while (true) {
        device_status_get(&status);
        ESP_LOGI(TAG, "display placeholder: state=%s err=%lu",
                 app_state_to_string(status.state),
                 (unsigned long)status.error_code);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void button_task(void *arg)
{
    (void)arg;

    while (true) {
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

    while (true) {
        device_status_get(&status);
        if (status.led_on != last_led) {
            device_led_set(status.led_on);
            last_led = status.led_on;
        }
        if (status.buzzer_on != last_buzzer) {
            device_buzzer_set(status.buzzer_on);
            last_buzzer = status.buzzer_on;
        }
        if (status.relay_on != last_relay) {
            device_relay_set(status.relay_on);
            last_relay = status.relay_on;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        device_status_tick(1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_create_placeholder_tasks(void)
{
    xTaskCreate(sensor_task, "sensor_task", 3072, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 3072, NULL, 4, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 6, NULL);
    xTaskCreate(control_task, "control_task", 3072, NULL, 5, NULL);
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "placeholder FreeRTOS tasks created");
}
