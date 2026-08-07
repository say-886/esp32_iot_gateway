# Qt / Linux 上位机客户端

本目录提供 ESP32 IoT Gateway 的 Qt 上位机客户端实现与构建说明。

当前客户端主实现位于：

- `qt_linux_client/qt_linux_client_qml/`

当前版本采用：

- Qt Quick / QML
- HTTP 作为主控制链路
- MQTT 作为可选实时增强链路
- SQLite 保存本地历史数据
- 同时保留 `qmake` 与 `CMake` 两套工程入口

## 目录结构

```text
qt_linux_client/
├─ CMakeLists.txt
├─ README.md
└─ qt_linux_client_qml/
   ├─ CMakeLists.txt
   ├─ qt_linux_client_qml.pro
   ├─ main.cpp
   ├─ main.qml
   ├─ AppController.h / AppController.cpp
   ├─ GatewayMqttClient.h / GatewayMqttClient.cpp
   ├─ DashboardPage.qml
   ├─ ControlPage.qml
   ├─ ConfigPage.qml
   ├─ OtaPage.qml
   ├─ ModbusPage.qml
   ├─ TelemetryChart.qml
   └─ LogPanel.qml
```

## 已实现功能

- 设备连接：输入 IP、端口、API Token，轮询 `/api/status`
- 实时监控：温度、湿度、光照、WiFi、MQTT、运行时间、固件版本、设备 ID、错误状态
- 执行器控制：LED、蜂鸣器、继电器
- 配置管理：读取和保存 `/api/config`
- OTA：提交 HTTPS 固件 URL 到 `/api/ota`
- 设备重启：通过 `/api/reboot` 提交重启请求
- Modbus：读取 `/api/modbus`
- 本地历史：SQLite 保存最近 7 天温湿度/光照数据，并绘制曲线
- MQTT 增强：读取设备返回的 topic，支持订阅和命令发布
- 可靠重连：约 1/2/4/8/16/30 秒指数退避并带随机抖动
- 新协议：支持嵌套 `data` 遥测、`cmd_ack` 和带 UUID/有效期的命令
- 幂等历史：SQLite 增加 `device_id/boot_id/seq/replayed` 并安全迁移旧表

## 当前实现说明

### 1. HTTP 主链路

`AppController` 基于 `QNetworkAccessManager`，已接入：

- `GET /api/status`
- `POST /api/control`
- `GET /api/config`
- `POST /api/config`
- `POST /api/ota`
- `POST /api/reboot`
- `GET /api/modbus`

鉴权接口统一使用：

```http
Authorization: Bearer <token>
```

默认策略：

- 状态轮询周期：`2000 ms`
- HTTP 超时：`5000 ms`

### 2. MQTT 增强链路

`GatewayMqttClient` 会在 HTTP 成功读取 `/api/status` 后，自动使用这些字段：

- `mqtt_host`
- `mqtt_port`
- `mqtt_use_tls`
- `mqtt_status_topic`
- `mqtt_sensor_topic`
- `mqtt_heartbeat_topic`
- `mqtt_error_topic`
- `mqtt_cmd_topic`
- `mqtt_cmd_ack_topic`

控制策略为：

- MQTT 可用时优先走 MQTT
- MQTT 不可用时回退到 HTTP
- MQTT 命令使用 QoS 1，并生成 `cmd_id`、`created_at`、`expires_at`
- 收到 `cmd_ack` 后在日志中显示 `executed/rejected/expired/duplicate`

### 3. 本地数据

- 使用 `QSettings` 保存最近连接参数
- 使用 SQLite 保存历史遥测数据
- 默认加载最近 `200` 条
- 默认保留最近 `7` 天
- 新遥测使用 `(device_id, boot_id, seq)` 唯一索引抵御 QoS 1 重复投递
- 已有旧数据库通过 `ALTER TABLE` 增量迁移，不删除原历史

## 开发与运行环境

### Windows 开发环境

当前已经验证过的环境：

- Qt Creator
- Qt 5.15.2 MinGW 64-bit
- `qmake` 工程

项目文件：

- `qt_linux_client/qt_linux_client_qml/qt_linux_client_qml.pro`

### Linux 运行环境

推荐目标环境：

- Ubuntu 22.04 / 24.04
- `cmake`
- `g++`
- Qt 5 开发包
- SQLite Qt 驱动
- 若需要 MQTT：Qt MQTT 开发包

常见依赖示例：

```bash
sudo apt update
sudo apt install -y \
  cmake \
  g++ \
  qtbase5-dev \
  qtdeclarative5-dev \
  qtquickcontrols2-5-dev \
  libqt5sql5-sqlite
```

如果需要 MQTT，再额外安装 Qt MQTT 开发包。不同 Ubuntu 版本包名可能略有差异，常见为：

- `libqt5mqtt5-dev`

如果系统仓库没有该包，也可以像当前 Windows 环境一样，从 Qt 官方 `qtmqtt` 源码单独编译安装。

## 构建方式

### 1. Qt Creator + qmake

适合当前 Windows 开发调试：

1. 打开 `qt_linux_client/qt_linux_client_qml/qt_linux_client_qml.pro`
2. 选择可用 Kit
3. 点击构建
4. 点击运行

### 2. 命令行 + qmake

Windows 已验证命令示例：

```powershell
cd "C:\desktop\ESP32 Project\qt_linux_client\qt_linux_client_qml\build\Desktop_Qt_5_15_2_MinGW_64_bit-Debug"
E:\qt\5.15.2\mingw81_64\bin\qmake.exe ..\..\qt_linux_client_qml.pro
$env:Path = "E:\qt\Tools\mingw810_64\bin;" + $env:Path
E:\qt\Tools\mingw810_64\bin\mingw32-make.exe -j4
```

### 3. 命令行 + CMake

这是推荐的 Linux 构建方式。

顶层入口已经补齐：

- `qt_linux_client/CMakeLists.txt`
- `qt_linux_client/qt_linux_client_qml/CMakeLists.txt`

Linux 示例命令：

```bash
cd /path/to/ESP32_Project
cmake -S qt_linux_client -B qt_linux_client/build
cmake --build qt_linux_client/build -j
```

生成完成后，可执行文件位于：

- `qt_linux_client/build/qt_linux_client_qml/qt_linux_client_qml`

如果需要安装到系统目录：

```bash
cmake --install qt_linux_client/build
```

## MQTT 模块说明

当前客户端在 `qmake` 和 `CMake` 下都做了“可选启用”处理：

- 如果检测到 Qt MQTT 模块，则启用真实 MQTT 功能
- 如果没有检测到 Qt MQTT 模块，则构建 HTTP-only stub

也就是说：

- 没装 Qt MQTT 也能编译、运行、测试 HTTP / SQLite / 配置 / OTA / Modbus
- 装了 Qt MQTT 后，可以继续做 MQTT 实机联调

对应代码位置：

- `qt_linux_client/qt_linux_client_qml/qt_linux_client_qml.pro`
- `qt_linux_client/qt_linux_client_qml/CMakeLists.txt`
- `qt_linux_client/qt_linux_client_qml/GatewayMqttClient.h`
- `qt_linux_client/qt_linux_client_qml/GatewayMqttClient.cpp`

## 接口对照

### `GET /api/status`

用途：

- 免鉴权
- 读取传感器数据
- 读取执行器状态
- 读取运行状态和 MQTT topic 元信息

### `POST /api/control`

示例：

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```

### `POST /api/ota`

示例：

```json
{
  "url": "https://example.com/esp32_iot_gateway.bin"
}
```

客户端侧已做校验：

- URL 不能为空
- 必须以 `https://` 开头

### `POST /api/reboot`

用途：

- 鉴权后触发设备重启
- 适合在保存配置后手动重启设备

### `GET /api/modbus`

用途：

- 查看 Modbus 使能状态
- 在线状态
- 寄存器值
- 成功/失败次数
- 最近错误

## 验收建议

### 基础连接

1. 输入设备 IP、端口、Token
2. 点击连接
3. 确认 `/api/status` 能正常返回
4. 确认 Dashboard 自动刷新

### 控制功能

1. 点击 LED / 蜂鸣器 / 继电器按钮
2. 观察设备实际动作
3. 确认 Dashboard 状态同步变化
4. 确认日志区出现控制结果

### 配置功能

1. 读取 `/api/config`
2. 修改采样周期或 MQTT 参数
3. 保存配置
4. 确认敏感字段不会被回填显示

### OTA 功能

1. 输入非 HTTPS 地址，确认客户端拦截
2. 输入 HTTPS 固件地址
3. 点击开始 OTA
4. 观察设备日志与客户端日志

### 重启功能

1. 点击“重启设备”
2. 观察客户端日志
3. 等待设备重新上线
4. 确认 `/api/status` 恢复正常

### Modbus 功能

1. 打开 Modbus 页面
2. 点击刷新
3. 检查寄存器值、成功次数、失败次数和最近错误

### MQTT 功能

1. 确认设备 `/api/status` 返回 `mqtt_host / mqtt_port / mqtt_*_topic`
2. 确认客户端日志出现 MQTT 连接和订阅信息
3. 验证 `status / sensor / heartbeat / error / cmd_ack` 上行 topic
4. 验证 `cmd` 下发后日志收到相同 `cmd_id` 的 ACK

## 下一步建议

- 在 Ubuntu 虚拟机中实际跑通 `cmake -S qt_linux_client -B qt_linux_client/build`
- 补 Linux 下打包脚本
- 增加多设备管理
- 增加历史数据导出
