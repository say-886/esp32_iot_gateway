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

## 实机验收命令

如果电脑已安装 Mosquitto 客户端，可以使用下面命令订阅上报：

```powershell
mosquitto_sub -h broker.emqx.io -p 1883 -t "esp32/gateway/#" -v
```

下发控制命令：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"led":true}'
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"led":false}'
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"buzzer":true}'
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"buzzer":false}'
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"relay":true}'
mosquitto_pub -h broker.emqx.io -p 1883 -t "esp32/gateway/cmd" -m '{"relay":false}'
```

验收标准：

- 串口出现 `MQTT connected`。
- 订阅端可以看到 `status`、`sensor`、`heartbeat` 上报。
- 向 `esp32/gateway/cmd` 发布命令后，串口出现 `MQTT command applied`，实物 LED、蜂鸣器或继电器同步动作。
