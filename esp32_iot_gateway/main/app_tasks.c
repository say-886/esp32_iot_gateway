#include "app_tasks.h"

#include <stddef.h>

#include "app_config.h"
#include "button.h"
#include "board.h"
#include "device_control.h"
#include "error_code.h"
#include "mqtt_service.h"
#include "oled_ssd1306.h"
#include "sensor_aht20.h"
#include "sensor_bh1750.h"
#include "storage_nvs.h"
#include "watchdog_service.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_tasks";
static const uint32_t DISPLAY_UPDATE_PERIOD_MS = 1000; // display_task更新周期（毫秒）
static const uint32_t STATUS_LOG_PERIOD_MS = 20000; // 状态日志记录周期（毫秒）
static const uint32_t MQTT_PUBLISH_PERIOD_MS = 5000; // MQTT 发布周期（毫秒）
static const uint32_t SENSOR_ERROR_THRESHOLD = 3; // 传感器错误阈值（次）
static const uint32_t SENSOR_ERROR_LOG_PERIOD = 10;     // 传感器错误日志记录周期（秒）
static TaskHandle_t s_task_handles[6];
static const char *s_task_names[6] = {
    "sensor_task", "display_task", "mqtt_publish_task",
    "button_task", "control_task", "monitor_task"
};

/**
 * @brief 从 NVS 加载采样周期配置。
 * 
 * @return uint32_t 返回有效的采样周期（毫秒），若加载失败或数值非法则返回默认值。
 */
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

/**
 * @brief 初始化设备状态结构体。
 * 
 * @param status 指向待初始化的状态结构体的指针。
 */
void app_status_init(device_status_t *status)
{
    device_status_init_default(status);
}

/**
 * @brief 传感器数据采集任务。
 * 
 * 定期从 AHT20 和 BH1750 读取环境数据（温湿度、光照），并更新全局状态。
 * 支持传感器故障检测与状态机自动降级。
 * 
 * @param arg 任务创建时的参数，当前未使用。
 */
static void sensor_task(void *arg)
{
    (void)arg;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float light = 0.0f;
    device_status_t status;
    uint32_t aht_fail_count = 0;
    uint32_t bh_fail_count = 0;
    bool last_read_ok = true;   // 上次读取是否成功

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());        

    while (true) {
        uint32_t sample_period_ms = app_get_sample_period_ms();
        device_status_get(&status);
        temperature = status.temperature;
        humidity = status.humidity;
        light = status.light;

        // 直接调用传感器读取函数，它们内部已经有锁保护
        // 由于使用了递归互斥锁，这样也是安全的
        esp_err_t aht_ret = aht20_read(&temperature, &humidity);
        esp_err_t bh_ret = bh1750_read(&light);

        if (aht_ret == ESP_OK) {
            aht_fail_count = 0;
            device_status_clear_error(APP_ERR_AHT20_READ_FAILED);
        } else {
            aht_fail_count++;
            if (aht_fail_count >= SENSOR_ERROR_THRESHOLD) {
                device_status_set_error(APP_ERR_AHT20_READ_FAILED);
            }
        }
        if (bh_ret == ESP_OK) {
            bh_fail_count = 0;
            device_status_clear_error(APP_ERR_BH1750_READ_FAILED);
        } else {
            bh_fail_count++;
            if (bh_fail_count >= SENSOR_ERROR_THRESHOLD) {
                device_status_set_error(APP_ERR_BH1750_READ_FAILED);
            }
        }

        if (aht_ret == ESP_OK && bh_ret == ESP_OK) {
            device_status_update_sensor(temperature, humidity, light);
            if (!last_read_ok) {
                ESP_LOGI(TAG, "sensor recovered: temp=%.2f C hum=%.2f %%RH light=%.2f lux",
                         temperature, humidity, light);
            }
            last_read_ok = true; // 标记为读取成功
        } else {
            if (aht_ret == ESP_OK || bh_ret == ESP_OK) {
                device_status_update_sensor(temperature, humidity, light);
            }
            if (last_read_ok ||
                aht_fail_count == SENSOR_ERROR_THRESHOLD ||
                bh_fail_count == SENSOR_ERROR_THRESHOLD ||
                (aht_fail_count > 0 && (aht_fail_count % SENSOR_ERROR_LOG_PERIOD) == 0U) ||
                (bh_fail_count > 0 && (bh_fail_count % SENSOR_ERROR_LOG_PERIOD) == 0U)) {
                ESP_LOGW(TAG, "sensor read degraded: AHT20=%s(%lu) BH1750=%s(%lu) threshold=%lu",
                         esp_err_to_name(aht_ret),
                         (unsigned long)aht_fail_count,
                         esp_err_to_name(bh_ret),
                         (unsigned long)bh_fail_count,
                         (unsigned long)SENSOR_ERROR_THRESHOLD);
            }
            last_read_ok = false;
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
    }
}

/**
 * @brief 显示与上报任务。
 * 
 * 负责 OLED 屏幕刷新、MQTT 数据发布以及串口运行状态日志打印。
 * 
 * @param arg 任务创建时的参数，当前未使用。
 */
static void display_task(void *arg)
{
    (void)arg;
    device_status_t status;
    uint32_t log_elapsed_ms = STATUS_LOG_PERIOD_MS; // 日志时间间隔
    device_state_t last_state = DEVICE_STATE_INIT; // 上次状态
    uint32_t last_error = APP_ERR_NONE; 

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_get(&status);
        if (board_i2c_lock() == ESP_OK) {
            oled_show_status(&status);
            board_i2c_unlock();
        }

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

        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_PERIOD_MS));
        log_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
    }
}

static void mqtt_publish_task(void *arg)
{
    (void)arg;
    device_status_t status;
    uint32_t publish_elapsed_ms = 0;
    uint32_t heartbeat_elapsed_ms = 0;

    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());
    while (true) {
        device_status_get(&status);
        publish_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
        heartbeat_elapsed_ms += DISPLAY_UPDATE_PERIOD_MS;
        if (publish_elapsed_ms >= MQTT_PUBLISH_PERIOD_MS) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_queue_sensor(&status));
            if (status.mqtt_connected) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_status(&status));
                if (status.error_code != APP_ERR_NONE) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_error(&status));
                }
            }
            publish_elapsed_ms = 0;
        }
        if (heartbeat_elapsed_ms >= APP_DEFAULT_HEARTBEAT_PERIOD_MS) {
            if (status.mqtt_connected) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_service_publish_heartbeat(&status));
            }
            heartbeat_elapsed_ms = 0;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed());
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_PERIOD_MS));
    }
}

/**
 * @brief 辅助函数：快速更新单个执行器的命令状态。
 * 
 * @param led_set 是否更新 LED
 * @param led_on LED 目标开关状态
 * @param buzzer_set 是否更新蜂鸣器
 * @param buzzer_on 蜂鸣器目标开关状态
 * @param relay_set 是否更新继电器
 * @param relay_on 继电器目标开关状态
 */
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

/**
 * @brief 本地按键扫描任务。
 * 
 * 扫描开发板上的按键输入，并根据按键事件触发对应的执行器控制。
 * 
 * @param arg 任务创建时的参数，当前未使用。
 */
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

/**
 * @brief 执行器同步任务。
 * 
 * 将共享状态中的目标开关值（来自 Web、MQTT 或按键）同步到真实的 GPIO 硬件驱动中。
 * 
 * @param arg 任务创建时的参数，当前未使用。
 */
static void control_task(void *arg)
{
    (void)arg;
    bool last_led = false;   /**< 上次 LED 状态 */
    bool last_buzzer = false; /**< 上次蜂鸣器状态 */
    bool last_relay = false; /**< 上次继电器状态 */
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

/**
 * @brief 系统监控与计时任务。
 * 
 * 维护系统运行时间，并执行看门狗喂狗操作以确保任务健康。
 * 
 * @param arg 任务创建时的参数，当前未使用。
 */
static void monitor_task(void *arg)
{
    (void)arg;
    uint32_t health_log_elapsed_sec = 0;
    ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_register_current_task());

    while (true) {
        device_status_tick(1);
        health_log_elapsed_sec++;
        if (health_log_elapsed_sec >= 20U) {
            ESP_LOGI(TAG, "health: free_heap=%lu largest_block=%lu monitor_stack_hwm=%lu",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL));
            for (size_t i = 0; i < sizeof(s_task_handles) / sizeof(s_task_handles[0]); i++) {
                if (s_task_handles[i] != NULL) {
                    ESP_LOGI(TAG, "health: task=%s stack_hwm=%lu",
                             s_task_names[i],
                             (unsigned long)uxTaskGetStackHighWaterMark(s_task_handles[i]));
                }
            }
            health_log_elapsed_sec = 0;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(watchdog_service_feed()); /* 喂狗 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief 创建并启动所有业务相关的 FreeRTOS 任务。
 */
static esp_err_t create_task_checked(TaskFunction_t task,
                                     const char *name,
                                     uint32_t stack_size,
                                     UBaseType_t priority,
                                     TaskHandle_t *handle)
{
    if (xTaskCreate(task, name, stack_size, NULL, priority, handle) != pdPASS) {
        ESP_LOGE(TAG, "failed to create task: %s", name);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_create_tasks(void)
{
    esp_err_t err = create_task_checked(sensor_task, s_task_names[0], 4096, 5, &s_task_handles[0]);
    if (err == ESP_OK) err = create_task_checked(display_task, s_task_names[1], 3072, 4, &s_task_handles[1]);
    if (err == ESP_OK) err = create_task_checked(mqtt_publish_task, s_task_names[2], 3072, 4, &s_task_handles[2]);
    if (err == ESP_OK) err = create_task_checked(button_task, s_task_names[3], 3072, 6, &s_task_handles[3]);
    if (err == ESP_OK) err = create_task_checked(control_task, s_task_names[4], 3072, 5, &s_task_handles[4]);
    if (err == ESP_OK) err = create_task_checked(monitor_task, s_task_names[5], 2048, 3, &s_task_handles[5]);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "hardware FreeRTOS tasks created");
    return ESP_OK;
}
