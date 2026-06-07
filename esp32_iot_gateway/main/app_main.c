#include "app_config.h"

#include "board.h"
#include "button.h"
#include "device_control.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2c_test.h"
#include "mqtt_service.h"
#include "oled_ssd1306.h"
#include "sensor_aht20.h"
#include "sensor_bh1750.h"
#include "storage_nvs.h"
#include "watchdog_service.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";
static device_status_t g_device_status;

/**
 * @brief 应用程序主入口。
 *
 * 该函数在系统启动时被调用。它负责按照依赖顺序初始化所有硬件组件和软件服务：
 * 1. 初始化共享设备状态内存。
 * 2. 初始化板级硬件（I2C、GPIO等）。
 * 3. 初始化执行器控制层和按键输入层。
 * 4. 初始化外设（OLED、AHT20、BH1750）。
 * 5. 初始化 NVS 存储。
 * 6. 启动网络服务（WiFi、Web Server）。
 * 7. 启动系统监视服务（看门狗）。
 * 8. 创建 FreeRTOS 业务任务。
 */
void app_main(void)
{
    /* 在其他服务使用状态前，先准备内存中的状态快照。 */
    app_status_init(&g_device_status);      /* 初始化应用状态 */
    device_status_store_init();

    ESP_LOGI(TAG, "Project: %s", APP_PROJECT_NAME);
    ESP_LOGI(TAG, "Firmware: %s", g_device_status.firmware_version);
    ESP_LOGI(TAG, "Current state: %s",
             app_state_to_string(g_device_status.state));
    ESP_LOGI(TAG, "HTTP APIs: %s, %s, %s, %s, %s",
             APP_HTTP_API_STATUS,
             APP_HTTP_API_CONTROL,
             APP_HTTP_API_CONFIG,
             APP_HTTP_API_REBOOT,
             APP_HTTP_API_OTA);
    ESP_LOGI(TAG, "MQTT topics: %s, %s, %s, %s, %s",
             APP_MQTT_TOPIC_STATUS,
             APP_MQTT_TOPIC_SENSOR,
             APP_MQTT_TOPIC_HEARTBEAT,
             APP_MQTT_TOPIC_CMD,
             APP_MQTT_TOPIC_ERROR);

    /* 按依赖顺序启动硬件和共享服务。 */
#if APP_ENABLE_I2C_TEST_MODE
    ESP_LOGW(TAG, "I2C test mode is enabled. WiFi/Web/MQTT services will not start.");
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(i2c_test_start());
    return;
#endif

    ESP_ERROR_CHECK(board_init());  // 初始化板级硬件（I2C、按键GPIO）
    ESP_ERROR_CHECK(device_control_init()); // 初始化执行器控制层（LED、蜂鸣器、继电器）
    ESP_ERROR_CHECK(button_init()); // 初始化按键输入层（4个按键）

    esp_err_t oled_ret = oled_init();       // 初始化 OLED 显示（准备显示温湿度看板）
       if (oled_ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED init skipped: %s. Check SSD1306 wiring/address.",
                 esp_err_to_name(oled_ret));
    } else {
        ESP_LOGI(TAG, "OLED init OK");
    }

    esp_err_t sensor_ret = aht20_init();     // 初始化 AHT20 温湿度传感器
    if (sensor_ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 init failed: %s. Check VCC/GND/SDA/SCL wiring.",
                 esp_err_to_name(sensor_ret));
        device_status_set_error(APP_ERR_AHT20_READ_FAILED);
    } else {
        ESP_LOGI(TAG, "AHT20 init OK");
    }

    sensor_ret = bh1750_init();     // 初始化 BH1750 光敏传感器
    if (sensor_ret != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 init failed: %s. Check VCC/GND/SDA/SCL wiring.",
                 esp_err_to_name(sensor_ret));
        device_status_set_error(APP_ERR_BH1750_READ_FAILED);
    } else {
        ESP_LOGI(TAG, "BH1750 init OK");
    }

    ESP_ERROR_CHECK(storage_nvs_init()); // 初始化 NVS 存储（用于存储应用配置和状态快照，如 WiFi 配置、MQTT 主题等）

#if APP_ENABLE_NETWORK_SERVICES
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(web_server_start());
#else
    ESP_LOGW(TAG, "Network services are disabled. WiFi/Web/MQTT will not start.");
#endif

    ESP_ERROR_CHECK(watchdog_service_init());

    /* 平台服务准备完成后，再启动演示任务。 */
    ESP_ERROR_CHECK(app_create_placeholder_tasks());
}
