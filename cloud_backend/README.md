# ESP32 云端设备管理后端

该服务使用 MQTT.js、Node 内置 SQLite 和原生 HTTP Server，实现多设备遥测、命令状态跟踪及本地看板。它不再依赖“每次命令临时创建一个 QoS 0 Socket”的方式。

## 环境

- Node.js `>= 22.5`（使用 `node:sqlite`）
- 支持 MQTT 3.1.1 的 Broker，推荐 TLS 8883

安装依赖：

```powershell
cd cloud_backend
npm install
```

## 配置与启动

```powershell
Copy-Item config.example.json config.local.json
npm start
```

`config.local.json` 已加入 `.gitignore`，不要提交真实账号和密码。主要配置：

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 3000,
    "commandTimeoutMs": 30000
  },
  "mqtt": {
    "protocol": "mqtts",
    "host": "broker.example.com",
    "port": 8883,
    "clientId": "gateway_backend_001",
    "username": "username",
    "password": "password",
    "rejectUnauthorized": true,
    "topicRoot": "esp32/gateway",
    "defaultDeviceId": "esp32_gateway_001"
  }
}
```

启动后访问 `http://localhost:3000`。数据库默认位于 `cloud_backend/data/iot_platform.db`。

## MQTT 业务

服务以 QoS 1 订阅：

```text
esp32/gateway/+/sensor
esp32/gateway/+/status
esp32/gateway/+/heartbeat
esp32/gateway/+/error
esp32/gateway/+/cmd_ack
```

- MQTT 使用持久会话和 30 秒 Keepalive。
- 重连采用约 `1/2/4/8/16/30` 秒指数退避并带抖动。
- 遥测通过 `(device_id, boot_id, seq)` 唯一约束去重。
- 命令收到 Broker PUBACK 后进入 `PUBLISHED`，设备 ACK 后进入 `ACKED`，过期进入 `TIMEOUT`，发布失败进入 `FAILED`。

## HTTP API

兼容接口：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/iot/sensor` | 兼容 EMQX Webhook 写入 |
| GET | `/api/sensor/latest?device_id=...` | 最新遥测 |
| GET | `/api/sensor/history?device_id=...&limit=50` | 历史遥测 |
| POST | `/api/cmd` | 向 `device_id` 或默认设备创建命令 |
| GET | `/api/status` | 平台、MQTT 和最近数据状态 |

设备管理接口：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/devices` | 设备列表 |
| GET | `/api/devices/:deviceId` | 单设备状态 |
| GET | `/api/devices/:deviceId/telemetry` | 单设备遥测 |
| GET | `/api/devices/:deviceId/commands` | 单设备命令历史 |
| POST | `/api/devices/:deviceId/commands` | 创建控制命令 |
| GET | `/api/commands/:cmdId` | 查询命令状态 |

创建命令：

```json
{
  "led": 1,
  "relay": 0,
  "ttl_ms": 30000
}
```

接口返回 `202` 和 `cmd_id`。前端可轮询 `/api/commands/:cmdId` 直到 `ACKED/TIMEOUT/FAILED`。

## 兼容 EMQX Webhook

虽然服务会直接订阅 MQTT，仍保留 Webhook 接口用于迁移或调试。请求体：

```json
{
  "clientid": "esp32_gateway_001",
  "topic": "esp32/gateway/esp32_gateway_001/sensor",
  "payload": {
    "device_id": "esp32_gateway_001",
    "boot_id": 1,
    "seq": 1,
    "data": {"temperature": 25, "humidity": 60, "light": 300}
  },
  "timestamp": 1786080000000
}
```

## 验证

```powershell
npm run check
```

本地接口测试覆盖 SQLite 初始化、新协议写入、重复序号去重、设备查询和命令入队。真实 Broker 的 TLS、ACL 和设备 ACK 仍需使用部署环境账号联调。
