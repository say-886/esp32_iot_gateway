# 系统架构与任务划分

## 系统架构图

```mermaid
flowchart LR
    subgraph INPUT[输入侧]
        BTN[按键模块 K1-K4]
        AHT[AHT20 温湿度]
        BH[BH1750 光照]
    end

    subgraph CORE[ESP32 网关核心]
        BOARD[board\nGPIO / I2C / 引脚约定]
        TASKS[FreeRTOS Tasks\nsensor / display / button / control / monitor]
        STATE[app_common + app_state\n状态快照 / 错误码 / 状态机]
        CTRL[device_control\nLED / 蜂鸣器 / 继电器]
        NVS[storage_nvs\n配置持久化]
        OLED[oled_ssd1306\n本地显示]
    end

    subgraph NET[联网与对外接口]
        WIFI[wifi_manager]
        WEB[web_server\n/api/status\n/api/control\n/api/config\n/api/reboot]
        MQTT[mqtt_service\nstatus / sensor / heartbeat / cmd / error]
        PAGE[Web 前端页面]
        BROKER[MQTT Broker]
    end

    BTN --> TASKS
    AHT --> BOARD
    BH --> BOARD
    BOARD --> TASKS
    TASKS --> STATE
    TASKS --> CTRL
    TASKS --> OLED
    NVS --> TASKS
    WIFI --> WEB
    WIFI --> MQTT
    STATE --> WEB
    STATE --> MQTT
    WEB --> PAGE
    MQTT --> BROKER
```

## 数据流说明

- `sensor_task` 周期读取 `AHT20`、`BH1750`，将数据写入全局状态快照。
- `button_task` 扫描 `K1~K4`，把用户输入转换成执行器控制意图。
- `control_task` 根据全局状态驱动 `LED`、蜂鸣器、继电器。
- `display_task` 周期刷新 `OLED`，同时承担状态日志与 MQTT 周期上报。
- `wifi_manager` 负责联网状态维护；联网后启动 `web_server` 与 `mqtt_service`。
- `web_server` 与 `mqtt_service` 都从统一状态快照读取数据，避免接口和页面各自维护状态。

## FreeRTOS 任务划分图

```mermaid
flowchart TB
    SENSOR[sensor_task\n2s 左右采样\n读取 AHT20 / BH1750]
    DISPLAY[display_task\n1s 刷新 OLED\n5s 上报状态]
    BUTTON[button_task\n20ms 扫描按键]
    CONTROL[control_task\n100ms 同步执行器]
    MONITOR[monitor_task\n1s 维护 uptime / 喂狗]
    WEBTASK[web_server\n事件驱动]
    MQTTTASK[mqtt_service\n事件驱动 + 周期发布]

    SENSOR --> DISPLAY
    SENSOR --> WEBTASK
    SENSOR --> MQTTTASK
    BUTTON --> CONTROL
    CONTROL --> DISPLAY
    MONITOR --> DISPLAY
```

## 当前模块边界

- `board`：统一维护 GPIO、I2C 端口、设备地址等板级约定。
- `app_common`：维护设备全局状态、命令结构、错误码。
- `app_state`：管理 `INIT / WIFI_CONNECTING / MQTT_CONNECTING / ONLINE / RECOVERY / ERROR`。
- `device_control`：封装 `LED`、蜂鸣器、继电器输出。
- `sensor_aht20` / `sensor_bh1750`：封装传感器读取。
- `oled_ssd1306`：封装 I2C OLED 初始化、清屏与状态显示。
- `wifi_manager` / `web_server` / `mqtt_service`：负责联网、接口与消息通道。
- `storage_nvs`：保存 WiFi、MQTT、采样周期等配置。

## 当前状态

- 已打通：按键、LED、蜂鸣器、继电器、WiFi、Web API、前端页面、MQTT。
- 已接入：AHT20、BH1750、OLED 驱动。
- 待继续验收：AHT20 长时间稳定性、OLED 实机显示、OTA、异常恢复场景。
