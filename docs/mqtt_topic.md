# MQTT Topic

| Topic | 方向 | 说明 |
| --- | --- | --- |
| `esp32/gateway/status` | 上行 | 上报设备整体状态 |
| `esp32/gateway/sensor` | 上行 | 上报温湿度和光照数据 |
| `esp32/gateway/heartbeat` | 上行 | 上报心跳 |
| `esp32/gateway/cmd` | 下行 | 接收控制命令 |
| `esp32/gateway/error` | 上行 | 上报错误信息 |

控制命令示例：

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```
