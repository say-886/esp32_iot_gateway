# ESP32 IoT Gateway

基于 ESP32 + ESP-IDF + FreeRTOS + Mongoose 的环境监测与远程控制网关。





## 工程目录

```text
esp32_iot_gateway/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── README.md
├── .gitignore
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c       # 主程序入口
│   ├── app_tasks.c      # 任务管理函数
│   ├── app_tasks.h      # 任务管理函数头文件
│   ├── app_config.h     # 应用配置头文件
│   └── app_version.h    # 应用版本头文件
├── components/            # 组件目录
│   ├── board/             # 板级组件
│   ├── app_common/        # 应用通用组件
│   ├── app_state/         # 应用状态组件
│   ├── device_control/    # 设备控制组件
│   ├── button/            # 按键组件
│   ├── sensor_aht20/      # AHT20 温湿度传感器组件
│   ├── sensor_bh1750/    # BH1750 光照传感器组件
│   ├── oled_ssd1306/     # OLED SSD1306 显示屏组件
│   ├── wifi_manager/     # WiFi 管理组件
│   ├── web_server/      # Web 服务器组件
│   ├── mqtt_service/    # MQTT 服务组件
│   ├── storage_nvs/      # NVS 存储组件
│   ├── watchdog_service/ # 看门狗服务组件
│   ├── ota_service/     # OTA 服务组件
│   └── mongoose/        # Mongoose 服务组件
├── web/
│   ├── index.html
│   ├── style.css
│   └── app.js
├── docs/
│   ├── hardware_connection.md
│   ├── system_architecture.md
│   ├── mqtt_topic.md
│   ├── api_design.md
│   ├── task_design.md
│   ├── debug_notes.md
│   ├── project_roadmap.md
│   └── interview_notes.md
└── tools/
    ├── mqtt_test_payload.json
    └── api_test.md
```

普通组件目录采用以下结构：

```text
component_name/
├── CMakeLists.txt
├── include/
│   └── component_name.h
└── component_name.c
```


## MQTT Topic 设计

| Topic | 方向 | 说明 |
| --- | --- | --- |
| `esp32/gateway/status` | 上行 | 上报设备整体状态 |
| `esp32/gateway/sensor` | 上行 | 上报传感器数据 |
| `esp32/gateway/heartbeat` | 上行 | 上报心跳 |
| `esp32/gateway/cmd` | 下行 | 接收控制命令 |
| `esp32/gateway/error` | 上行 | 上报错误信息 |

## 状态与错误码

设备状态：

| 状态 | 含义 |
| --- | --- |
| `INIT` | 系统初始化 |
| `WIFI_CONNECTING` | WiFi 连接中 |
| `MQTT_CONNECTING` | MQTT 连接中 |
| `ONLINE` | 正常在线运行 |
| `ERROR` | 出现异常 |
| `RECOVERY` | 异常恢复中 |

错误码：

| 错误码 | 含义 |
| --- | --- |
| `0` | 无错误 |
| `1001` | WiFi 连接失败 |
| `1002` | MQTT 连接失败 |
| `2001` | AHT20 读取失败 |
| `2002` | BH1750 读取失败 |
| `3001` | NVS 读取失败 |
| `3002` | NVS 写入失败 |
| `4001` | OTA 失败 |
| `5001` | 看门狗异常 |



## OTA 分区

当前 `partitions.csv` 已切换为 OTA 分区布局：

| 分区 | 用途 | 大小 |
| --- | --- | --- |
| `nvs` | 配置保存 | 16K |
| `otadata` | OTA 状态数据 | 8K |
| `phy_init` | RF 参数 | 4K |
| `ota_0` | 应用分区 A | 1536K |
| `ota_1` | 应用分区 B | 1536K |

当前固件大小约 1.0MB，仍可放入 1.5MB OTA 分区。

