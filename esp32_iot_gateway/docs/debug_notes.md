# 常见问题与调试记录

## 1. 首页一直显示 `INIT / 未连接 / 0 s`

### 现象

- 浏览器能打开首页，但页面始终停留在默认假数据。
- `/api/status` 已能返回真实 JSON。

### 根因

- 设备端静态资源发送方式存在结尾空字符问题。
- 浏览器缓存旧版 `app.js`，导致页面未执行最新脚本。

### 处理

- 修正静态资源发送逻辑。
- 重新烧录固件。
- 使用 `Ctrl + F5` 强制刷新页面。

## 2. HTTP 服务器启动时崩溃

### 现象

- 串口出现：
  `ESP_ERR_HTTPD_HANDLERS_FULL`

### 根因

- 新增页面静态资源路由后，默认 URI handler 槽位不够。

### 处理

- 增大 `httpd_config_t.max_uri_handlers`。

## 3. MQTT 启动后立刻 DNS 失败或连接超时

### 现象

- 串口出现 `getaddrinfo()` 失败或 `select() timeout`。

### 根因

- `MQTT` 在 `WiFi got ip` 之前启动，网络栈尚未具备完整解析/连接条件。

### 处理

- 调整为 `WiFi got ip` 后再启动 `mqtt_service`。
- 断网时同步停止 MQTT，避免保留脏会话状态。

## 4. 页面状态已变，但蜂鸣器/继电器无反应

### 现象

- `/api/status` 中 `buzzer` / `relay` 状态已改变。
- 物理模块无反应。

### 根因

- 实物接线问题，而不是软件逻辑问题。

### 处理

- 重新核对 `GPIO25`、`GPIO26`、`VCC`、`GND` 和信号线接法。
- 确认模块触发电平与当前代码一致。

## 5. 只有 K1、K4 有效，K2、K3 无反应

### 现象

- `K1`、`K4` 正常控制，`K2`、`K3` 无效。

### 根因

- 按键模块对应线接错或面包板连线位置错误。

### 处理

- 重新核对：
  - `K1 -> GPIO27`
  - `K2 -> GPIO14`
  - `K3 -> GPIO32`
  - `K4 -> GPIO33`
- 确认按键模块公共端接 `GND`，按下为低电平触发。

## 6. `AHT20` 长时间失败后又恢复

### 现象

- 串口会出现：
  - `sensor read degraded`
  - `sensor recovered`
- 状态会在 `RECOVERY` 与 `ONLINE` 间切换。

### 根因

- 读取流程原先只用固定延时，缺少更稳妥的忙位轮询和数据完整性检查。

### 处理

- 为 `AHT20` 增加忙位轮询。
- 增加 CRC 校验。
- 连续异常时执行初始化与软复位恢复。

## 7. `build.ninja` / `ninja` 在 Windows 下偶发失败

### 现象

- 构建时出现：
  - `failed recompaction: Permission denied`
  - `GetOverlappedResult`

### 根因

- Windows 文件占用、杀毒扫描或旧进程未退出。

### 处理

- 关闭旧终端与旧构建进程。
- 必要时删除 `build/` 后重新构建。
- 保持同一时间只有一个 `idf.py build/flash` 在运行。

## 8. `OLED` 无法初始化

### 现象

- 启动日志出现 `OLED init skipped` 或 `ESP_ERR_NOT_FOUND`。

### 根因

- 常见于：
  - `VCC/GND/SDA/SCL` 接线错误
  - 实际地址不是 `0x3C/0x3D`
  - 模块不是标准 I2C SSD1306

### 处理

- 检查 I2C 接线与共地。
- 确认模块地址。
- 优先使用标准 4 针 I2C SSD1306 OLED。

## 9. 2026-05-25 实机验收记录

### 已通过

- `AHT20/DHT20` 能读取温湿度，偶发 I2C 失败后可恢复。
- `BH1750` 能读取光照。
- `WiFi STA` 能连接并获取 IP：`10.135.247.46`。
- `MQTT` 能连接 `broker.emqx.io:1883`。
- `K1~K4` 按键输入正常。
- `LED`、蜂鸣器、继电器可由按键和 Web API 控制。
- Web 页面和 `/api/status`、`/api/control` 已实机验收。

### 软件调整

- 传感器任务改为连续 3 次失败才设置错误码，避免单次 I2C 抖动导致系统频繁进入 `RECOVERY`。
- MQTT 命令 payload 改为先复制到本地缓冲区再解析，避免未以 `\0` 结尾的数据造成越界读取。
- 看门狗恢复为真实任务注册和喂狗。
- 新增 `/api/ota`，可通过 HTTP/HTTPS 固件 URL 触发 OTA 升级。

### 待继续验证

- MQTT 订阅上报与下发控制命令完整验收。
- OTA 使用局域网 HTTP 固件文件进行升级验收。
- OLED 到货后再进行 I2C 地址扫描与显示验收。
