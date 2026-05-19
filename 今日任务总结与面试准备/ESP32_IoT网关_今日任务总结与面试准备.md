# ESP32 IoT 网关今日任务总结与面试准备

## 1. 当前阶段说明

当前项目处于 ESP32 硬件到货前准备版 V0 阶段，重点是先完成工程骨架、模块边界、接口设计、状态结构和演示页面，为后续硬件联调做准备。

今日已经完成或整理的内容包括：

- ESP-IDF 工程结构和 component 化模块拆分。
- 设备状态结构、控制命令结构、错误码和运行状态设计。
- FreeRTOS 任务骨架，包括传感器、显示、按键、控制、监控等任务。
- HTTP API 和 MQTT Topic 设计。
- Web 假数据页面，用于提前演示状态展示和远程控制流程。
- NVS 配置持久化接口骨架。
- Wi-Fi Station 初始化、事件处理和断线重连逻辑骨架。
- ESP-IDF 编译验证，当前 README 中记录为 ESP-IDF v5.3.2、target 为 esp32、build passed。
- 关键 C 接口补充中文 Doxygen 注释，方便后续维护和面试讲解。

需要明确的是：当前版本不声明已经完成真实硬件烧录、传感器读取、Wi-Fi 联网实测、MQTT 联调、OLED 显示、蜂鸣器/继电器真实控制或 OTA 验证。这些内容需要等硬件到货后逐项验证。

## 2. 今日完成任务

### 2.1 工程结构与模块拆分

项目采用 ESP-IDF component 方式组织代码，主工程位于 `esp32_iot_gateway/`。整体思路是让 `main/` 只负责启动流程和任务创建，具体功能放到 `components/` 中维护。

主要模块职责如下：

| 模块 | 职责 |
| --- | --- |
| `main` | 应用入口、初始化顺序、FreeRTOS 任务创建 |
| `app_common` | 设备状态、控制命令、错误码等公共数据结构 |
| `board` | 板级 GPIO 初始化和硬件抽象 |
| `device_control` | LED、蜂鸣器、继电器等执行器控制 |
| `wifi_manager` | Wi-Fi Station 初始化、启动、事件处理 |
| `web_server` | HTTP 服务和 REST API |
| `mqtt_service` | MQTT 上报和命令订阅预留 |
| `storage_nvs` | NVS 配置存储 |
| `watchdog_service` | 看门狗服务预留 |
| `sensor_aht20` | AHT20 温湿度采集预留 |
| `sensor_bh1750` | BH1750 光照采集预留 |
| `oled_ssd1306` | OLED 显示预留 |

这种拆分便于面试时说明“模块边界清晰、后续硬件驱动可替换、网络与控制逻辑不互相耦合”。

### 2.2 共享设备状态与控制命令

`device_status_t` 用于描述设备当前状态，包含温度、湿度、光照、LED、蜂鸣器、继电器、Wi-Fi、MQTT、运行时间、错误码、固件版本等字段。

`device_cmd_t` 用于描述控制命令，使用 `*_set` 表示是否要修改某个执行器，用 `*_value` 表示目标值。这样可以支持局部控制，例如只控制 LED，而不影响蜂鸣器和继电器。

今日补充的关键接口说明包括：

- `device_status_init_default()`：填充默认状态。
- `device_status_store_init()`：初始化模块内部全局状态。
- `device_status_get()`：向调用者返回当前状态快照。
- `device_status_update_sensor()`：更新温湿度和光照。
- `device_status_update_control()`：根据控制命令更新目标执行器状态。
- `device_status_update_network()`：根据 Wi-Fi/MQTT 连接状态推导设备高层状态。
- `device_status_tick()`：累计运行时间。

### 2.3 FreeRTOS 任务骨架

当前已经建立多个演示任务，用于表达后续完整系统的数据流：

| 任务 | 当前职责 | 后续替换方向 |
| --- | --- | --- |
| `sensor_task` | 周期性生成模拟温湿度和光照数据 | 替换为 AHT20、BH1750 真实读取 |
| `display_task` | 周期性读取状态并打印日志 | 替换为 OLED 刷新 |
| `button_task` | 按键扫描任务占位 | 加入 GPIO 输入和软件消抖 |
| `control_task` | 根据共享状态同步执行器目标值 | 接入真实 LED、蜂鸣器、继电器 |
| `monitor_task` | 周期性增加 uptime | 接入心跳、看门狗、错误监控 |

这种设计的好处是先把任务职责和调度模型搭起来，硬件到货后只需要把模拟逻辑替换为真实驱动逻辑。

### 2.4 NVS 配置持久化

`storage_nvs` 模块负责保存应用配置，例如 Wi-Fi SSID、Wi-Fi 密码、MQTT URI、采样周期等。

实现要点：

- `storage_nvs_init()` 初始化 NVS Flash。
- 如果 NVS 分区空间不足或版本不兼容，会擦除后重新初始化。
- `storage_load_config()` 先填入默认配置，再从 NVS 读取 blob。
- 如果 NVS 里还没有配置，默认值仍然可用，避免结构体内容未初始化。
- `storage_save_config()` 将配置整体写入 NVS。
- `storage_reset_config()` 用默认配置覆盖当前配置。

面试中可以强调：先填默认值是为了提高首次启动和异常恢复的可靠性。

### 2.5 Wi-Fi Station 管理

`wifi_manager` 模块负责初始化 ESP-IDF 网络接口、事件循环、Wi-Fi 驱动，并以 Station 模式启动。

实现要点：

- `wifi_manager_init()` 初始化网络接口和事件处理器。
- `wifi_manager_start()` 从 NVS 加载 Wi-Fi 配置，并调用 ESP-IDF Wi-Fi API 启动 Station。
- `wifi_event_handler()` 处理 `WIFI_EVENT_STA_START`、`WIFI_EVENT_STA_DISCONNECTED`、`IP_EVENT_STA_GOT_IP` 等事件。
- Station 启动后调用 `esp_wifi_connect()`。
- 断开后更新共享网络状态，并尝试重新连接。
- 获取 IP 后标记 Wi-Fi 已连接，并更新设备状态。

当前实现是联网流程骨架，真实联网效果需要硬件和有效路由器配置验证。

### 2.6 HTTP API

`web_server` 模块实现了内嵌 HTTP 服务骨架，当前核心接口是：

| 接口 | 方法 | 用途 |
| --- | --- | --- |
| `/api/status` | GET | 返回当前设备状态 JSON |
| `/api/control` | POST | 接收 LED、蜂鸣器、继电器控制命令 |

`GET /api/status` 的数据来自 `device_status_get()`，本质是读取当前共享状态快照，然后拼成 JSON 返回。

`POST /api/control` 当前使用轻量字符串匹配解析简单 JSON 布尔字段，例如 `led`、`buzzer`、`relay`。这种方式适合演示阶段，但正式版本建议替换为 cJSON 等可靠 JSON 解析库，以支持空格、字段顺序、复杂类型和错误提示。

### 2.7 MQTT Topic 与 Web 页面

MQTT Topic 设计如下：

| Topic | 方向 | 说明 |
| --- | --- | --- |
| `esp32/gateway/status` | 上行 | 上报设备整体状态 |
| `esp32/gateway/sensor` | 上行 | 上报温湿度和光照数据 |
| `esp32/gateway/heartbeat` | 上行 | 上报心跳 |
| `esp32/gateway/cmd` | 下行 | 接收控制命令 |
| `esp32/gateway/error` | 上行 | 上报错误信息 |

Web 页面当前用于硬件到货前演示：即使没有真实设备，也可以提前展示环境监测、状态刷新和控制入口的交互形态。

## 3. 如何实现：核心数据流

### 3.1 启动流程

`app_main()` 是系统入口，按依赖顺序拉起核心服务：

1. 初始化内存中的设备状态。
2. 初始化 `device_status` 全局状态存储。
3. 打印项目版本、HTTP 接口和 MQTT Topic。
4. 初始化板级驱动。
5. 初始化设备控制层。
6. 初始化 NVS。
7. 初始化 Wi-Fi。
8. 启动 Wi-Fi Station。
9. 启动 HTTP Server。
10. 初始化看门狗服务。
11. 创建 FreeRTOS 演示任务。

这个顺序的关键点是：共享状态和配置存储要在网络、控制、Web 服务使用前准备好。

### 3.2 传感器数据更新流程

当前 `sensor_task` 使用模拟值代替真实传感器数据：

1. 周期性生成温度、湿度、光照模拟值。
2. 调用 `device_status_update_sensor()` 写入共享状态。
3. 打印日志，便于观察任务是否在运行。
4. 后续接入真实硬件时，将模拟值替换为 AHT20 和 BH1750 的 I2C 读取结果。

### 3.3 Web 状态查询流程

状态查询接口的数据流：

1. 浏览器请求 `GET /api/status`。
2. `status_handler()` 调用 `device_status_get()`。
3. 获取当前状态快照。
4. 组装 JSON 响应。
5. 浏览器或前端页面显示最新状态。

这种方式把 Web 层和底层任务解耦，Web 层不直接访问传感器或 GPIO。

### 3.4 Web 控制执行器流程

控制接口的数据流：

1. 浏览器请求 `POST /api/control`。
2. 请求体中包含 `led`、`buzzer`、`relay` 等字段。
3. `control_handler()` 解析请求体并构造 `device_cmd_t`。
4. 调用 `device_status_update_control()` 更新共享目标状态。
5. `control_task` 周期性读取状态。
6. 当发现目标状态变化时，调用 `device_led_set()`、`device_buzzer_set()`、`device_relay_set()` 同步到板级执行器。

当前控制任务只在状态变化时访问硬件，避免重复设置 GPIO。

### 3.5 Wi-Fi 网络状态更新流程

Wi-Fi 状态来自 ESP-IDF 事件回调：

1. `WIFI_EVENT_STA_START`：Station 启动，进入连接流程。
2. `WIFI_EVENT_STA_DISCONNECTED`：标记 Wi-Fi 断开，更新共享状态，并尝试重连。
3. `IP_EVENT_STA_GOT_IP`：获取 IP，标记 Wi-Fi 已连接，更新共享状态。

网络状态最终写入 `device_status_t`，供 Web、MQTT 和日志统一使用。

## 4. 面试可能提问与回答要点

### Q1：为什么采用 ESP-IDF component 方式拆分项目？

回答要点：

- ESP-IDF 本身推荐 component 化管理复杂工程。
- 可以把硬件驱动、网络服务、状态管理、存储、Web 服务拆成独立模块。
- `main` 只负责初始化流程，不堆业务细节。
- 后续替换传感器、增加 OTA、增加 Modbus/CAN 时，不需要大改主流程。
- 面试中可以强调这是为了可维护性、可扩展性和职责边界清晰。

### Q2：为什么使用 FreeRTOS 多任务，而不是一个 while 循环写完？

回答要点：

- 网关类项目同时有传感器采集、显示刷新、按键扫描、网络通信、控制输出、状态监控等并行职责。
- 每类任务周期不同：传感器可能 2 秒一次，按键可能 10 到 20 毫秒一次，Web/MQTT 是事件驱动。
- FreeRTOS 可以把不同周期和优先级的工作拆开，减少单个大循环的复杂度。
- 后续可以进一步用队列、事件组、互斥锁优化任务间通信。

### Q3：共享状态是怎么设计的？

回答要点：

- 用 `device_status_t` 表示设备完整运行状态。
- 用 `device_cmd_t` 表示控制命令，支持局部更新。
- 各模块不直接互相调用底层细节，而是围绕共享状态读写。
- Web 查询读状态，传感器任务写传感器字段，Wi-Fi 事件写网络字段，控制任务读目标状态并同步硬件。

当前需要补充说明：现阶段共享状态还没有加互斥锁，适合演示骨架。后续真实多任务并发读写时，应使用 mutex、critical section 或队列，避免并发读写造成状态不一致。

### Q4：NVS 读取配置时为什么先填默认值？

回答要点：

- 首次启动时 NVS 里可能没有配置。
- 如果直接读取失败后使用结构体，可能出现未初始化内容。
- 先写默认值，再尝试读取 NVS，可以保证配置始终有合理兜底。
- 如果 NVS 中存在配置，就用持久化配置覆盖默认值。
- 这种方式对首次启动、恢复出厂设置、异常恢复都更稳。

### Q5：Wi-Fi 断线重连逻辑怎么做？

回答要点：

- 通过 ESP-IDF 事件系统监听 Wi-Fi 和 IP 事件。
- Station 启动后调用 `esp_wifi_connect()`。
- 如果收到断开事件，标记本地状态为未连接，并再次调用 `esp_wifi_connect()`。
- 如果收到获取 IP 事件，说明 Wi-Fi 已真正连接成功，再更新共享状态。
- 这样比单纯看启动结果更准确，因为网络连接是异步事件。

### Q6：HTTP 控制接口怎么解析？有什么局限？

回答要点：

- 当前为了演示阶段轻量实现，用字符串匹配判断请求体中是否有 `led`、`buzzer`、`relay`，以及值是否为 `true` 或 `1`。
- 优点是简单、依赖少、方便快速跑通链路。
- 局限是对复杂 JSON、空格格式、错误输入、嵌套结构支持不足。
- 正式版本应改为 cJSON 等 JSON 解析库，并增加参数校验和错误响应。

### Q7：MQTT Topic 为什么这样设计？

回答要点：

- 上行和下行分离，便于云端订阅和权限管理。
- `status` 表示设备整体状态。
- `sensor` 表示高频传感器数据。
- `heartbeat` 表示设备在线心跳。
- `cmd` 表示云端下发控制命令。
- `error` 表示异常和错误码上报。
- 这种拆分便于后续做云平台规则引擎、告警、日志分析。

### Q8：哪些是已经真实完成的，哪些还只是预留？

回答要点：

已经完成：

- 工程骨架。
- component 模块划分。
- 设备状态和控制命令结构。
- HTTP/MQTT 接口设计。
- Web 假数据页面。
- NVS、Wi-Fi、Web Server 等代码骨架。
- ESP-IDF 编译验证。
- 关键接口注释补全。

仍待硬件验证：

- ESP32 实机烧录和串口日志。
- AHT20、BH1750、OLED 的 I2C 地址扫描和驱动验证。
- LED、蜂鸣器、继电器 GPIO 控制。
- 按键输入和消抖。
- Wi-Fi 使用真实 SSID/密码联网。
- MQTT Broker 连接、订阅、发布。
- OTA 和看门狗实际效果。

面试中不能把预留功能说成已经硬件实测完成，可以说“已经完成软件框架和接口设计，硬件到货后按计划逐项验证”。

### Q9：后续如何接入 AHT20、BH1750 和 OLED？

回答要点：

1. 先确定 ESP32 开发板引脚，填写最终 I2C SDA/SCL。
2. 编写 I2C Scanner，确认设备地址。
3. 在 `sensor_aht20` 中实现温湿度读取。
4. 在 `sensor_bh1750` 中实现光照读取。
5. 在 `oled_ssd1306` 中实现初始化、清屏、字符串显示。
6. 将 `sensor_task` 中的模拟值替换为真实读取值。
7. 将 `display_task` 中的日志输出替换为 OLED 刷新。
8. 保持上层 `device_status_t` 不变，这样 Web/MQTT 不需要大改。

### Q10：后续如何接入 OTA 和看门狗？

回答要点：

- 看门狗用于防止任务卡死，可以在关键任务周期性喂狗，监控任务异常。
- OTA 用于远程升级固件，需要 HTTP/HTTPS 下载固件、校验、写入 OTA 分区并重启切换。
- 当前项目已经预留 `watchdog_service` 和 `ota_service` 模块，后续可以在不破坏主框架的情况下补充实现。
- OTA 必须注意分区表、固件签名或校验、断电恢复和版本回滚策略。

## 5. 当前边界与后续计划

### 5.1 当前边界

当前项目是“硬件到货前准备版”，重点是软件框架、接口设计和演示链路。当前不要在简历或面试中说成已经完成真实硬件闭环。

可以准确表述为：

> 我已经完成 ESP32 IoT 网关的软件工程骨架、模块拆分、状态模型、HTTP/MQTT 接口设计、Web 演示页面和 ESP-IDF 编译验证。硬件到货后，会按 I2C 扫描、传感器读取、OLED 显示、GPIO 控制、Wi-Fi/MQTT 联调的顺序逐项验证。

### 5.2 后续计划

后续建议按以下顺序推进：

1. ESP32 到货后完成串口识别、烧录和 hello_world 验证。
2. 跑通 LED、蜂鸣器、继电器 GPIO 输出，确定最终引脚表。
3. 实现按键输入和软件消抖。
4. 实现 I2C Scanner，确认 AHT20、BH1750、OLED 地址。
5. 接入 AHT20 温湿度读取。
6. 接入 BH1750 光照读取。
7. 接入 OLED 本地显示。
8. 用真实传感器数据替换 `sensor_task` 中的模拟数据。
9. 使用真实 Wi-Fi 配置验证 Station 连接。
10. 接入 MQTT Broker，验证状态上报和命令下发。
11. 完善 JSON 解析、并发保护、错误码和恢复策略。
12. 增加 OTA 和看门狗实测。

## 6. 面试表达建议

面试中建议围绕“项目目标、工程拆分、数据流、可靠性、后续验证”展开，不要只背模块名。

可以按下面顺序介绍：

1. 项目是基于 ESP32 的环境监测与远程控制网关。
2. 使用 ESP-IDF 和 FreeRTOS，按 component 方式拆分模块。
3. 传感器、Web、MQTT、控制任务都围绕统一的设备状态结构交互。
4. Web 端通过 HTTP 查询状态和下发控制命令。
5. MQTT 负责后续云端上报和远程控制。
6. NVS 用于保存 Wi-Fi、MQTT、采样周期等配置。
7. 当前已完成硬件到货前的软件准备和编译验证，真实硬件功能会按计划逐项联调。

如果被追问当前不足，可以主动说明：

- 当前 JSON 解析还是演示级实现，正式版本会换成 cJSON。
- 当前共享状态还没有加互斥保护，真实多任务并发下会补 mutex 或队列。
- 当前传感器和 OLED 是接口预留，硬件到货后才做 I2C 扫描和驱动验证。
- 当前 MQTT、OTA、看门狗是模块预留，还需要联调和异常场景测试。

这种回答比单纯说“都做好了”更可信，也更符合工程实际。
