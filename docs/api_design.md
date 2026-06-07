# HTTP API Design

| 接口 | 方法 | 用途 |
| --- | --- | --- |
| `/api/status` | GET | 获取设备当前状态 |
| `/api/control` | POST | 控制 LED、蜂鸣器、继电器 |
| `/api/config` | GET | 获取当前配置 |
| `/api/config` | POST | 设置采样周期、MQTT 地址、告警阈值 |
| `/api/reboot` | POST | 设备重启 |
| `/api/ota` | POST | 通过 HTTP/HTTPS URL 执行 OTA 升级 |

`POST /api/control` 请求示例：

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```

`POST /api/ota` 请求示例：

```json
{
  "url": "http://10.135.247.100/esp32_iot_gateway.bin"
}
```

说明：

- OTA 会下载 URL 指向的固件并写入升级分区，成功后自动重启。
- 首次测试建议使用局域网 HTTP 服务器，避免公网网络波动影响判断。
- OTA 前必须确认 `partitions.csv` 已预留足够 app 分区空间。
