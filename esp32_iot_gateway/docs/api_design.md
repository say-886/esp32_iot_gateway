# HTTP API Design

| 接口 | 方法 | 用途 |
| --- | --- | --- |
| `/api/status` | GET | 获取设备当前状态 |
| `/api/control` | POST | 控制 LED、蜂鸣器、继电器 |
| `/api/config` | GET | 获取当前配置 |
| `/api/config` | POST | 设置采样周期、MQTT 地址、告警阈值 |
| `/api/reboot` | POST | 设备重启 |

`POST /api/control` 请求示例：

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```
