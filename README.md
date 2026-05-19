# ESP32 IoT Gateway

基于 ESP32 + ESP-IDF + FreeRTOS + Mongoose 的环境监测与远程控制网关。

当前阶段是硬件到货前准备版 V0，目标是先完成项目结构、接口设计、文档和 Web 假数据页面。当前不声明已经完成烧录、传感器读取、WiFi、MQTT 或真实硬件控制。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Git 仓库 | 已初始化 | 根目录为 `C:\desktop\ESP32 Project` |
| ESP-IDF 工程骨架 | 已创建 | 位于 `esp32_iot_gateway/` |
| Web 假数据页面 | 已创建 | 可直接打开 `web/index.html` |
| ESP-IDF 编译验证 | 已完成 | ESP-IDF v5.3.2 安装在 `E:\ESP`，`idf.py build` 已通过 |
| 硬件验证 | 待完成 | ESP32 与外设尚未到货 |

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

## 接线表模板

硬件到货后先确认开发板丝印，再填写最终 GPIO。以下仅作为模板，不作为已验证接线。

| 模块 | 信号 | ESP32 GPIO | 电源 | 备注 |
| --- | --- | --- | --- | --- |
| AHT20 | SDA | 待定 | 3.3V | I2C |
| AHT20 | SCL | 待定 | 3.3V | I2C |
| BH1750 | SDA | 待定 | 3.3V | I2C，可与 AHT20 共线 |
| BH1750 | SCL | 待定 | 3.3V | I2C，可与 AHT20 共线 |
| OLED SSD1306 | SDA | 待定 | 3.3V | I2C，可与传感器共线 |
| OLED SSD1306 | SCL | 待定 | 3.3V | I2C，可与传感器共线 |
| LED | IN | 待定 | 3.3V | GPIO 输出 |
| 蜂鸣器 | IN | 待定 | 3.3V/5V | GPIO 输出 |
| 继电器 | IN | 待定 | 3.3V/5V | GPIO 输出 |
| 按键 | OUT | 待定 | 3.3V | GPIO 输入，需要消抖 |

## 工程目录

```text
esp32_iot_gateway/
├── assets/
│   ├── images/
│   └── videos/
├── CMakeLists.txt
├── docs/
│   ├── api/
│   ├── architecture/
│   ├── daily_logs/
│   └── hardware/
├── main/
│   ├── app_main.c
│   ├── app_tasks.c
│   ├── app_config.h
│   └── CMakeLists.txt
├── components/
│   ├── app_common/
│   ├── app_state/
│   ├── device_control/
│   ├── sensor_aht20/
│   ├── sensor_bh1750/
│   ├── oled_ssd1306/
│   ├── web_server/
│   ├── mqtt_service/
│   └── storage_nvs/
├── managed_components/
├── scripts/
├── tools/
├── web/
│   ├── index.html
│   ├── style.css
│   └── app.js
├── sdkconfig
└── partitions.csv
```

每个 `components/*` 模块都预留了以下结构：

```text
component_name/
├── CMakeLists.txt
├── include/
│   └── component_name.h
└── src/
```

## HTTP API 设计

| 接口 | 方法 | 用途 | 当前状态 |
| --- | --- | --- | --- |
| `/api/status` | GET | 获取设备当前状态 | 已预留 |
| `/api/control` | POST | 控制 LED、蜂鸣器、继电器 | 已预留 |
| `/api/config` | GET | 获取当前配置 | 已预留 |
| `/api/config` | POST | 设置采样周期、MQTT 地址、告警阈值 | 已预留 |
| `/api/reboot` | POST | 设备重启 | 已预留 |

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

## 后续每日任务入口

硬件到货后从 D4 开始推进：

1. D4：连接开发板，确认串口，烧录 hello_world，记录烧录命令和串口号。
2. D5：跑通 LED 与蜂鸣器 GPIO 输出，确定最终 GPIO 表。
3. D6：实现按键输入和软件消抖。
4. D7：实现 I2C Scanner，确认 AHT20、BH1750、OLED 地址。
5. D8-D10：实现 AHT20、BH1750、OLED，完成基础传感器闭环。

## 不夸大声明

当前版本只完成硬件到货前准备工作。真实硬件驱动、烧录、串口日志、传感器数据、WiFi、Mongoose、MQTT、NVS、看门狗和 OTA 都需要在后续阶段逐项验证后再写入简历或项目成果。
