# ESP32 IoT Gateway

基于 ESP32 + ESP-IDF + FreeRTOS + Mongoose 的环境监测与远程控制网关。

当前版本已从基础 MQTT 上传扩展为端边云可靠链路：设备端具备指数退避重连、QoS 1、LWT、流量控制、Flash 离线队列、PUBACK 后出队和本地边缘计算；云端具备多设备登记、遥测幂等和命令状态机；Qt 客户端支持新协议、命令 ACK 和数据库迁移。完整需求、实现方案和验收边界见 [`docs/reliability_design.md`](docs/reliability_design.md)。





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
│   ├── offline_store/   # Flash 离线遥测队列
│   ├── edge_compute/    # 本地平滑与异常检测
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
│   └── project_roadmap.md
├── tools/
│   ├── mqtt_test_payload.json
│   └── api_test.md
├── cloud_backend/       # 多设备管理与遥测服务
└── qt_linux_client/     # Qt/QML 桌面客户端
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
| `esp32/gateway/<device_id>/status` | 上行 | 上报设备整体状态与 LWT |
| `esp32/gateway/<device_id>/sensor` | 上行 | 上报原始、边缘和补传遥测 |
| `esp32/gateway/<device_id>/heartbeat` | 上行 | 上报心跳和流量指标 |
| `esp32/gateway/<device_id>/cmd` | 下行 | 接收带 ID 和有效期的控制命令 |
| `esp32/gateway/<device_id>/cmd_ack` | 上行 | 上报命令执行结果 |
| `esp32/gateway/<device_id>/error` | 上行 | 上报错误信息 |

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
| `telemetry` | 离线遥测队列 | 896K |
| `coredump` | 崩溃转储 | 64K |

当前固件大小约 1.0MB，仍可放入 1.5MB OTA 分区。

新增 `telemetry` 分区后，旧设备首次部署必须完整烧录分区表；普通 OTA 只更新应用分区，不会创建新的数据分区。

## 端边云可靠能力

- 设备端：持久会话、LWT、QoS 1、指数退避、发送速率限制、Flash 离线队列和边缘异常检测。
- 云端：SQLite 多设备登记、遥测去重、命令下发与 ACK 状态跟踪，使用说明见 [`cloud_backend/README.md`](cloud_backend/README.md)。
- Qt 客户端：实时监控、设备控制、配置、OTA、Modbus、历史曲线和 MQTT 增强，使用说明见 [`qt_linux_client/README.md`](qt_linux_client/README.md)。

