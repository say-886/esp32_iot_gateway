# ESP32 IoT Gateway

基于 ESP32 + ESP-IDF + FreeRTOS + Mongoose 的环境监测与远程控制网关。

当前版本已从基础 MQTT 上传扩展为端边云可靠链路：设备端具备指数退避重连、QoS 1、LWT、流量控制、Flash 离线队列、PUBACK 后出队和本地边缘计算；云端具备多设备登记、遥测幂等、命令状态机；Qt 客户端支持新协议、命令 ACK 和数据库迁移。完整需求与实现边界见 [`docs/reliability_design.md`](docs/reliability_design.md)。

当前阶段已完成 ESP-IDF 环境打通、工程编译烧写监视验证，以及按键、执行器、WiFi、Web API、前端页面、MQTT 主流程联调。当前仍需继续收口 OLED 实机显示、AHT20 长时间稳定性、OTA 和异常恢复等剩余能力。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Git 仓库 | 已初始化 | 根目录为 `C:\desktop\ESP32 Project` |
| ESP-IDF 工程 | 已可编译、烧写、串口监视 | 命令行环境已验证可用 |
| VS Code 构建 | 已打通 | 可通过 `tasks.json` 构建，不依赖 ESP-IDF 扩展 |
| AHT20 | 已接入并增强稳定性 | I2C 地址 `0x38`，已支持忙位轮询、CRC 校验与失败恢复 |
| BH1750 | 已验证 | I2C 地址 `0x23`，光照读取正常 |
| I2C 总线接线 | 已确认 | 当前使用 `GPIO21/22` |
| Web 前端 | 已支持设备端页面访问 | ESP32 现可直接提供 `/`、`/style.css`、`/app.js` 与 API |
| 完整工程模式 | 已恢复 | 默认不再停留在 I2C Test Mode |
| OLED | 已接入 SSD1306 驱动 | 启动时自动探测 `0x3C/0x3D`，在显示任务中刷新状态 |
| 降级运行 | 已支持 | AHT20 异常时仍可继续验证 WiFi、Web API、MQTT 与 GPIO 控制 |
| MQTT 可靠传输 | 已实现并通过构建 | 持久会话、LWT、QoS 1、指数退避、outbox 高水位 |
| Flash 离线队列 | 已实现并通过构建 | CRC、双元数据日志、单条补传、PUBACK 后出队 |
| 边缘计算 | 已实现并通过构建 | EMA、阈值规则、传感器突变检测 |
| 云端设备管理 | 已通过本地接口测试 | SQLite 多设备、遥测去重、命令状态机 |

## 学习路线

如果只具备 STM32 与 FreeRTOS 基础，建议先阅读 `docs/learning_route.md`。该文档按当前代码实际入口整理了学习顺序，包括 `app_main()`、FreeRTOS 任务、GPIO/I2C、WiFi/Web API/MQTT、NVS、看门狗和 OTA，并附带 7 天学习计划、每日问题汇总模板、简历写法与面试问题清单。

## 硬件清单

| 序号 | 硬件名称 | 作用 | 备注 |
| --- | --- | --- | --- |
| 1 | ESP32-WROOM-32 开发板，30Pin，已焊排针 | 主控 | 推荐已焊排针版 |
| 2 | Micro USB 数据线 | 供电、烧录、串口日志 | 必须是数据线 |
| 3 | 面包板 | 模块连接 | 便于免焊接调试 |
| 4 | 杜邦线三合一套装 | 模块连接 | 公对公、公对母、母对母 |
| 5 | AHT20 温湿度传感器模块 | 温湿度采集 | I2C 接口 |
| 6 | BH1750 光照传感器模块 | 光照采集 | I2C 接口 |
| 7 | 0.96 寸 OLED 显示屏 | 本地数据显示 | I2C 接口 |
| 8 | LED 模块 | 状态指示/控制对象 | 建议 3~5 个 |
| 9 | 按键模块 | 本地输入 | 建议 2~4 个 |
| 10 | 有源蜂鸣器模块 | 告警输出 | 3.3V/5V 兼容优先 |
| 11 | 单路继电器模块 | 模拟执行器控制 | 3.3V 可触发优先 |

## 当前引脚约定

当前已在工程中固定并使用以下引脚约定，详细接线请看 `docs/hardware_connection.md`：

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| I2C SDA | `GPIO21` | AHT20、BH1750 共用 |
| I2C SCL | `GPIO22` | AHT20、BH1750 共用 |
| LED | `GPIO2` | 输出控制 |
| 蜂鸣器 | `GPIO25` | 输出控制，触发电平待实机确认 |
| 继电器 | `GPIO26` | 输出控制，触发电平待实机确认 |
| 按键 K1 | `GPIO27` | 短按切换 LED |
| 按键 K2 | `GPIO14` | 短按切换蜂鸣器 |
| 按键 K3 | `GPIO32` | 短按切换继电器 |
| 按键 K4 | `GPIO33` | 短按全部关闭 |

说明：

- `GPIO0` 保留给开发板 `BOOT` 键，不再作为外接按键使用。
- `OLED` 与 AHT20、BH1750 共用 `GPIO21/22` 的 I2C 总线，实机接线请参考 `docs/hardware_connection.md`。

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

## HTTP API 设计

| 接口 | 方法 | 用途 | 当前状态 |
| --- | --- | --- | --- |
| `/api/status` | GET | 获取设备当前状态 | 已实现，待联网实测 |
| `/api/control` | POST | 控制 LED、蜂鸣器、继电器 | 已实现，待联网实测 |
| `/api/config` | GET | 获取当前配置 | 已实现，待联网实测 |
| `/api/config` | POST | 设置采样周期、MQTT 地址、告警阈值 | 已实现，待联网实测 |
| `/api/reboot` | POST | 设备重启 | 已实现，待联网实测 |
| `/api/ota` | POST | 通过 HTTP/HTTPS URL 执行 OTA 升级 | 已实现，待局域网固件升级实测 |

`GET /api/status` 示例：

```json
{
  "temperature": 26.5,
  "humidity": 60.2,
  "light": 380,
  "led": 1,
  "buzzer": 0,
  "relay": 0,
  "wifi": 1,
  "mqtt": 0,
  "uptime": 12345,
  "error_code": 0,
  "firmware": "v0.1.0-prep"
}
```

`POST /api/control` 示例：

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```

## MQTT Topic 设计

| Topic | 方向 | 说明 |
| --- | --- | --- |
| `esp32/gateway/<device_id>/status` | 上行 | 上报设备整体状态与 LWT |
| `esp32/gateway/<device_id>/sensor` | 上行 | 上报原始、边缘和补传遥测 |
| `esp32/gateway/<device_id>/heartbeat` | 上行 | 上报心跳和流量指标 |
| `esp32/gateway/<device_id>/cmd` | 下行 | 接收带 ID/有效期的控制命令 |
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

## 当前已验证

- `ESP-IDF` 命令行环境可用。
- 工程已可编译、烧写、串口监视。
- `BH1750` 已验证地址 `0x23`，光照读取正常。
- `I2C` 总线接线已确认正确。
- `按键`、`LED`、`蜂鸣器`、`继电器` 已完成实机联调。
- `WiFi`、前端页面、`Web API`、`MQTT` 主流程已打通。
- `VS Code` 已可通过 `tasks.json` 执行构建。

## 当前待验收

- `OLED` 代码已接入，但仍需完成实机接线与显示验收。
- `AHT20` 已从持续失败恢复到可读状态，但还需继续观察长时间稳定性。
- `GET /api/config`、`POST /api/config`、`POST /api/reboot` 仍建议补一轮完整验收。
- `POST /api/ota` 已接入 HTTP/HTTPS OTA，仍需使用局域网固件文件做一次完整升级验收。
- `NVS`、看门狗、异常恢复仍需按场景补充长时间验证。

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

注意：新增 `telemetry` 分区后，旧设备首次部署必须完整烧录分区表；普通 OTA 只更新应用分区，不会创建新数据分区。

当前固件大小约 1.0MB，仍可放入 1.5MB OTA 分区。

## 本地私有配置

- 真实 `WiFi SSID/密码` 不建议直接写进仓库源码。
- 工程现已支持本地私有配置文件：
  `components/storage_nvs/include/storage_nvs_local.h`
- 该文件已加入 `.gitignore`，不会被 Git 提交。
- 示例模板见：
  `components/storage_nvs/include/storage_nvs_local.example.h`

## D0-D3 完成情况

| 天数 | 目标 | 当前结果 |
| --- | --- | --- |
| D0 | 创建仓库、README 初稿、硬件清单、接线表模板、项目名称和模块划分 | 已完成 |
| D1 | 安装工具链、设置目标芯片、编译工程 | ESP-IDF v5.3.2 已安装到 `E:\ESP`，`idf.py set-target esp32` 和 `idf.py build` 已通过 |
| D2 | 新建工程结构，建立 `main/`、`components/`、`web/`，添加配置和入口文件 | 已完成骨架 |
| D3 | 设计状态结构、命令结构、错误码、MQTT Topic、HTTP API，写 Web 假数据页面 | 已完成 |

## ESP-IDF 环境步骤

ESP-IDF 已安装到 `E:\ESP\frameworks\esp-idf-v5.3.2`，工具链和 Python 环境位于 `E:\ESP\tools`。已配置用户级环境变量：

| 变量 | 值 |
| --- | --- |
| `IDF_PATH` | `E:\ESP\frameworks\esp-idf-v5.3.2` |
| `IDF_TOOLS_PATH` | `E:\ESP\tools` |
| `IDF_PYTHON_ENV_PATH` | `E:\ESP\tools\python_env\idf5.3_py3.12_env` |

用户级 `PATH` 已加入 ESP-IDF 工具链目录。重新打开 PowerShell 或 cmd 后，通常可以直接运行：

```powershell
idf.py --version
idf.py build
```

如果当前终端还没刷新环境变量，可以临时运行：

```powershell
$env:IDF_TOOLS_PATH = "E:\ESP\tools"
& "E:\ESP\frameworks\esp-idf-v5.3.2\export.ps1"
```

也可以运行已准备好的备用脚本：

```powershell
& "E:\ESP\activate_esp_idf.ps1"
```

cmd 终端可运行：

```bat
E:\ESP\activate_esp_idf.bat
```

然后在工程目录执行：

```powershell
cd "C:\desktop\ESP32 Project\esp32_iot_gateway"
idf.py set-target esp32
idf.py build
```

当前已验证通过的版本：

```text
ESP-IDF v5.3.2
target: esp32
build: passed
```

## Qt / Linux 上位机客户端

上位机客户端说明已单独整理到：

- [`qt_linux_client/README.md`](../qt_linux_client/README.md)

当前客户端已覆盖：

- 设备连接
- 实时监控
- 执行器控制
- 配置管理
- OTA
- Modbus
- 本地历史曲线
- MQTT 增强
- 设备重启

## 后续每日任务入口

建议按以下顺序继续推进：

1. 切回完整工程模式后，逐个验证 `LED`、蜂鸣器、继电器、按键。
2. 写入真实 `WiFi` 配置，联调 `Web API`。
3. 验证 `MQTT` 五个主题的真实收发。
4. 等新 `OLED` 到货后，再做地址扫描、初始化与显示联调。
5. 最后做 `OTA`、`NVS`、看门狗和异常恢复场景验证。

## 不夸大声明

当前版本已经完成环境搭建、工程构建烧写监视验证，以及 `AHT20/BH1750/I2C` 基础联调；但 `WiFi`、`Web API`、`MQTT`、全部 GPIO、`OLED`、`OTA`、`NVS` 异常恢复、看门狗场景等仍需要继续实机验证后，才能写入简历或项目成果。
