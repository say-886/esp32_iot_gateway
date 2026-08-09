# ESP32 IoT Gateway

基于 ESP-IDF 5.3.2 的环境采集与 Modbus-MQTT 网关。设备端包含 AHT20、
BH1750、OLED、GPIO 执行器、FreeRTOS 六任务、局域网 Web 管理、MQTTS、
896 KiB 离线遥测队列、看门狗和 A/B OTA 回滚。

## 构建

```powershell
Set-ExecutionPolicy -Scope Process Bypass
& "C:\Espressif\v5.3.2\esp-idf\export.ps1"
idf.py build
```

首次烧录必须包含分区表：

```powershell
idf.py -p COMx flash monitor
```

## 安全配置

复制
`components/storage_nvs/include/storage_nvs_local.example.h` 为
`storage_nvs_local.h`，填写真实 Wi-Fi、私有 MQTTS Broker、Broker 用户名/
密码以及至少 16 字符的随机 API Token。该私有文件已加入 `.gitignore`。

安全默认行为：

- SNTP 时间有效后才启动 MQTTS；
- 拒绝明文 MQTT、已知公共演示 Broker、空 Broker 凭据和默认/弱 Token；
- MQTT 控制命令必须携带 `cmd_id`、`expires_at` 和 `auth`；
- 默认 Token 下，HTTP 修改接口保持禁用；HTTP 管理面仅用于可信局域网；
- OTA 仅接受 HTTPS，并在 60 秒任务健康窗口后确认新镜像。

## 验证边界

当前固件已在 ESP-IDF 5.3.2 下完整构建。弱网、补传中掉电、72 小时运行和
多轮 OTA 回滚仍需结合真实 ESP32 与私有 Broker 完成硬件验收。

`cloud_backend` 在当前导出包中缺少服务端入口和包清单，只有静态前端资源；
因此云端 SQLite/命令状态机不能作为当前可复现交付能力。
