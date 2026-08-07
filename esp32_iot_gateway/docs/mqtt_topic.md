# MQTT Topic 与可靠传输协议

## Topic 结构

所有 Topic 都包含 `device_id`，云端可通过 `+` 通配符统一管理多台设备。

| Topic | 方向 | QoS | retain | 说明 |
| --- | --- | ---: | ---: | --- |
| `esp32/gateway/<device_id>/status` | 上行 | 1 | 是 | 在线状态、执行器、固件、队列和边缘异常 |
| `esp32/gateway/<device_id>/sensor` | 上行 | 1 | 否 | 原始遥测、边缘计算和补传身份 |
| `esp32/gateway/<device_id>/heartbeat` | 上行 | 1 | 否 | 运行时间、网络和流量指标 |
| `esp32/gateway/<device_id>/error` | 上行 | 1 | 否 | 当前错误码和错误位图 |
| `esp32/gateway/<device_id>/cmd` | 下行 | 1 | 否 | 带唯一 ID 和有效期的控制命令 |
| `esp32/gateway/<device_id>/cmd_ack` | 上行 | 1 | 否 | 命令执行、拒绝、过期或重复确认 |

设备使用持久会话、30 秒 Keepalive，并在 `status` Topic 配置 QoS 1 retained LWT：

```json
{
  "schema": 1,
  "device_id": "esp32_gateway_001",
  "online": false
}
```

## 遥测协议

```json
{
  "schema": 1,
  "device_id": "esp32_gateway_001",
  "boot_id": 123456,
  "seq": 42,
  "timestamp": 1786080000000,
  "time_valid": true,
  "uptime_ms": 125000,
  "replayed": false,
  "data": {
    "temperature": 26.5,
    "humidity": 60.2,
    "light": 380
  },
  "edge": {
    "temperature_ema": 26.2,
    "humidity_ema": 59.8,
    "light_ema": 365,
    "anomaly_flags": 0
  },
  "error_code": 0
}
```

- `boot_id + seq`：同一设备内唯一的采样身份，云端以 `(device_id, boot_id, seq)` 去重。
- `timestamp`：设备完成校时后使用 Unix 毫秒；未校时时为 `0`，并令 `time_valid=false`。
- `replayed`：该样本采集时 MQTT 是否离线。无论在线还是离线，样本都先写入 Flash。
- `edge`：设备本地的指数移动平均和异常规则结果，断云时仍可工作。

## 命令与 ACK

下发命令：

```json
{
  "schema": 1,
  "cmd_id": "0bd28065-6263-4a16-a461-7923706caa71",
  "type": "control",
  "created_at": 1786080000000,
  "expires_at": 1786080030000,
  "payload": {
    "led": 1,
    "relay": 0
  }
}
```

设备确认：

```json
{
  "schema": 1,
  "device_id": "esp32_gateway_001",
  "cmd_id": "0bd28065-6263-4a16-a461-7923706caa71",
  "status": "executed",
  "code": 0,
  "timestamp": 1786080000123,
  "reported": {
    "led": 1,
    "buzzer": 0,
    "relay": 0
  }
}
```

设备端 ACK 状态：

- `executed`：命令已应用。
- `rejected`：类型或参数不合法。
- `expired`：设备已校时且命令超过有效期。
- `duplicate`：与最近执行的 `cmd_id` 相同，不重复驱动硬件。

为兼容旧客户端，设备仍接受顶层 `{"led":1}` 格式，但新系统应使用命令 envelope。

## 重连、流控和补传

1. Broker 断开后按约 `1/2/4/8/16/30` 秒指数退避，加入约 ±20% 抖动。
2. ESP-MQTT outbox 上限为 16 KiB；补传在 12 KiB 高水位暂停。
3. Flash 补传始终只保留一条遥测 inflight，收到对应 QoS 1 PUBACK 才出队。
4. CRC 损坏记录会计数后跳过，避免阻塞后续数据。
5. 队列满或当前扇区仍含未确认数据时丢弃最新样本，保留更早的待补传记录。

## 订阅验收

```powershell
mosquitto_sub -h <broker> -p 8883 --cafile <ca.pem> `
  -u <username> -P <password> -t "esp32/gateway/+/sensor" -q 1 -v
```

下发命令时将 `<device_id>` 替换为真实设备 ID，并使用上面的命令 envelope。
