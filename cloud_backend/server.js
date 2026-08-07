"use strict";

/**
 * ESP32 网关云端服务。
 *
 * 主要职责：
 * 1. 通过 MQTT.js 维持 QoS 1 长连接并订阅全部设备主题；
 * 2. 使用 SQLite 保存设备、遥测和命令全生命周期；
 * 3. 通过 device_id + boot_id + seq 对补传遥测做幂等去重；
 * 4. 提供兼容旧看板的接口以及面向多设备管理的新接口。
 */

const fs = require("fs");
const http = require("http");
const path = require("path");
const crypto = require("crypto");
const mqtt = require("mqtt");
const { DatabaseSync } = require("node:sqlite");

const ROOT = __dirname;
const DATA_DIR = process.env.IOT_DATA_DIR
  ? path.resolve(process.env.IOT_DATA_DIR)
  : path.join(ROOT, "data");
const DATABASE_FILE = path.join(DATA_DIR, "iot_platform.db");
const CONFIG_FILE = process.env.IOT_CONFIG_FILE
  ? path.resolve(process.env.IOT_CONFIG_FILE)
  : path.join(ROOT, "config.local.json");
const CONFIG_EXAMPLE_FILE = path.join(ROOT, "config.example.json");

function readJsonFile(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function loadConfig() {
  if (process.env.IOT_CONFIG_FILE && !fs.existsSync(CONFIG_FILE)) {
    throw new Error(`IOT_CONFIG_FILE does not exist: ${CONFIG_FILE}`);
  }
  const file = fs.existsSync(CONFIG_FILE) ? CONFIG_FILE : CONFIG_EXAMPLE_FILE;
  const loaded = readJsonFile(file);
  loaded.server ??= {};
  loaded.mqtt ??= {};
  loaded.server.host ??= "0.0.0.0";
  loaded.server.port ??= 3000;
  loaded.server.commandTimeoutMs ??= 30000;
  loaded.mqtt.protocol ??= loaded.mqtt.port === 8883 ? "mqtts" : "mqtt";
  loaded.mqtt.topicRoot ??= "esp32/gateway";
  loaded.mqtt.clientId ??= `gateway_backend_${crypto.randomUUID().slice(0, 8)}`;
  loaded.mqtt.defaultDeviceId ??= "esp32_gateway_001";
  return loaded;
}

const config = loadConfig();
fs.mkdirSync(DATA_DIR, { recursive: true });

/**
 * 打开数据库并启用 WAL。WAL 可降低 MQTT 写入与 HTTP 查询并发时的锁竞争。
 */
const database = new DatabaseSync(DATABASE_FILE);
database.exec(`
  PRAGMA journal_mode = WAL;
  PRAGMA synchronous = NORMAL;
  PRAGMA foreign_keys = ON;

  CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    online INTEGER NOT NULL DEFAULT 0,
    first_seen TEXT NOT NULL,
    last_seen TEXT NOT NULL,
    firmware TEXT,
    state TEXT,
    last_status_json TEXT,
    last_heartbeat_json TEXT,
    last_error_json TEXT
  );

  CREATE TABLE IF NOT EXISTS telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    boot_id INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    timestamp_ms INTEGER NOT NULL,
    received_at TEXT NOT NULL,
    replayed INTEGER NOT NULL DEFAULT 0,
    temperature REAL NOT NULL,
    humidity REAL NOT NULL,
    light REAL NOT NULL,
    temperature_ema REAL,
    humidity_ema REAL,
    light_ema REAL,
    edge_anomaly_flags INTEGER NOT NULL DEFAULT 0,
    error_code INTEGER NOT NULL DEFAULT 0,
    raw_json TEXT NOT NULL,
    UNIQUE(device_id, boot_id, seq)
  );

  CREATE INDEX IF NOT EXISTS idx_telemetry_device_time
    ON telemetry(device_id, timestamp_ms DESC);

  CREATE TABLE IF NOT EXISTS commands (
    cmd_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    type TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at TEXT NOT NULL,
    published_at TEXT,
    acked_at TEXT,
    expires_at INTEGER NOT NULL,
    ack_status TEXT,
    result_code INTEGER,
    ack_json TEXT,
    error TEXT
  );

  CREATE INDEX IF NOT EXISTS idx_commands_device_created
    ON commands(device_id, created_at DESC);
  CREATE INDEX IF NOT EXISTS idx_commands_status_expiry
    ON commands(status, expires_at);
`);

const statements = {
  upsertDevice: database.prepare(`
    INSERT INTO devices (
      device_id, online, first_seen, last_seen, firmware, state,
      last_status_json, last_heartbeat_json, last_error_json
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(device_id) DO UPDATE SET
      online = excluded.online,
      last_seen = excluded.last_seen,
      firmware = COALESCE(excluded.firmware, devices.firmware),
      state = COALESCE(excluded.state, devices.state),
      last_status_json = COALESCE(excluded.last_status_json, devices.last_status_json),
      last_heartbeat_json = COALESCE(excluded.last_heartbeat_json, devices.last_heartbeat_json),
      last_error_json = COALESCE(excluded.last_error_json, devices.last_error_json)
  `),
  insertTelemetry: database.prepare(`
    INSERT OR IGNORE INTO telemetry (
      device_id, boot_id, seq, timestamp_ms, received_at, replayed,
      temperature, humidity, light, temperature_ema, humidity_ema, light_ema,
      edge_anomaly_flags, error_code, raw_json
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  `),
  insertCommand: database.prepare(`
    INSERT INTO commands (
      cmd_id, device_id, type, payload_json, status, created_at, expires_at
    ) VALUES (?, ?, ?, ?, 'PENDING', ?, ?)
  `),
  nextPendingCommands: database.prepare(`
    SELECT cmd_id, device_id, payload_json, expires_at
    FROM commands
    WHERE status = 'PENDING' AND expires_at > ?
    ORDER BY created_at ASC
    LIMIT 20
  `),
  markPublished: database.prepare(`
    UPDATE commands
    SET status = 'PUBLISHED', published_at = ?, error = NULL
    WHERE cmd_id = ? AND status = 'PENDING'
  `),
  markPublishFailed: database.prepare(`
    UPDATE commands
    SET status = 'FAILED', error = ?
    WHERE cmd_id = ? AND status = 'PENDING'
  `),
  markTimedOut: database.prepare(`
    UPDATE commands
    SET status = 'TIMEOUT', error = COALESCE(error, 'command acknowledgement timeout')
    WHERE status IN ('PENDING', 'PUBLISHED') AND expires_at <= ?
  `),
  markAcknowledged: database.prepare(`
    UPDATE commands
    SET status = 'ACKED', acked_at = ?, ack_status = ?, result_code = ?,
        ack_json = ?, error = NULL
    WHERE cmd_id = ? AND device_id = ? AND status IN ('PENDING', 'PUBLISHED', 'TIMEOUT')
  `)
};

function nowIso() {
  return new Date().toISOString();
}

function finiteNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function safeJsonParse(value) {
  if (Buffer.isBuffer(value)) {
    value = value.toString("utf8");
  }
  if (typeof value === "string") {
    return JSON.parse(value);
  }
  return value;
}

/**
 * 从 esp32/gateway/<device_id>/<message_type> 中解析设备和消息类型。
 */
function parseTopic(topic) {
  const root = String(config.mqtt.topicRoot).replace(/\/+$/, "");
  const prefix = `${root}/`;
  if (!topic.startsWith(prefix)) {
    return null;
  }
  const parts = topic.slice(prefix.length).split("/");
  if (parts.length !== 2 || !parts[0] || !parts[1]) {
    return null;
  }
  return { deviceId: parts[0], messageType: parts[1] };
}

function touchDevice(deviceId, payload, messageType) {
  const timestamp = nowIso();
  const online = messageType === "status" && payload.online === false ? 0 : 1;
  statements.upsertDevice.run(
    deviceId,
    online,
    timestamp,
    timestamp,
    payload.firmware ?? null,
    payload.state ?? null,
    messageType === "status" ? JSON.stringify(payload) : null,
    messageType === "heartbeat" ? JSON.stringify(payload) : null,
    messageType === "error" ? JSON.stringify(payload) : null
  );
}

/**
 * 统一新旧遥测协议。新协议读取 data/edge 嵌套字段，旧协议继续读取顶层字段。
 */
function normalizeTelemetry(input, context = {}) {
  const payload = safeJsonParse(input);
  if (!payload || typeof payload !== "object") {
    throw new Error("telemetry payload must be a JSON object");
  }
  const topicInfo = context.topic ? parseTopic(context.topic) : null;
  const data = payload.data && typeof payload.data === "object" ? payload.data : payload;
  const edge = payload.edge && typeof payload.edge === "object" ? payload.edge : {};
  const deviceId = String(
    payload.device_id || context.deviceId || topicInfo?.deviceId || context.clientId || "unknown"
  ).trim();
  if (!deviceId || deviceId === "unknown") {
    throw new Error("device_id is required");
  }

  const temperature = Number(data.temperature);
  const humidity = Number(data.humidity);
  const light = Number(data.light);
  if (![temperature, humidity, light].every(Number.isFinite)) {
    throw new Error("temperature, humidity and light must be finite numbers");
  }

  const receivedAt = context.receivedAt || nowIso();
  const receivedMs = Date.parse(receivedAt);
  const timestampMs = Math.trunc(finiteNumber(payload.timestamp, context.timestamp || receivedMs));
  const bootId = Math.trunc(finiteNumber(payload.boot_id, 0));
  /* 旧协议没有 seq，使用接收毫秒和随机尾数生成兼容序号，避免错误去重。 */
  const legacySeq = receivedMs * 1000 + crypto.randomInt(0, 1000);
  const seq = Math.trunc(finiteNumber(payload.seq, legacySeq));

  return {
    device_id: deviceId,
    boot_id: bootId,
    seq,
    timestamp_ms: timestampMs,
    received_at: receivedAt,
    replayed: Boolean(payload.replayed),
    temperature,
    humidity,
    light,
    temperature_ema: edge.temperature_ema == null ? null : finiteNumber(edge.temperature_ema),
    humidity_ema: edge.humidity_ema == null ? null : finiteNumber(edge.humidity_ema),
    light_ema: edge.light_ema == null ? null : finiteNumber(edge.light_ema),
    edge_anomaly_flags: Math.trunc(finiteNumber(edge.anomaly_flags, 0)),
    error_code: Math.trunc(finiteNumber(payload.error_code, 0)),
    raw_json: JSON.stringify(payload)
  };
}

function saveTelemetry(record) {
  const result = statements.insertTelemetry.run(
    record.device_id,
    record.boot_id,
    record.seq,
    record.timestamp_ms,
    record.received_at,
    record.replayed ? 1 : 0,
    record.temperature,
    record.humidity,
    record.light,
    record.temperature_ema,
    record.humidity_ema,
    record.light_ema,
    record.edge_anomaly_flags,
    record.error_code,
    record.raw_json
  );
  touchDevice(record.device_id, JSON.parse(record.raw_json), "sensor");
  return result.changes > 0;
}

function handleCommandAck(deviceId, payload) {
  const cmdId = String(payload.cmd_id || "").trim();
  if (!cmdId) {
    return;
  }
  statements.markAcknowledged.run(
    nowIso(),
    String(payload.status || "unknown"),
    Math.trunc(finiteNumber(payload.code, 0)),
    JSON.stringify(payload),
    cmdId,
    deviceId
  );
}

function handleMqttMessage(topic, rawPayload) {
  const topicInfo = parseTopic(topic);
  if (!topicInfo) {
    return;
  }
  let payload;
  try {
    payload = safeJsonParse(rawPayload);
  } catch (error) {
    console.warn(`[mqtt] invalid JSON topic=${topic}: ${error.message}`);
    return;
  }

  try {
    if (topicInfo.messageType === "sensor") {
      const record = normalizeTelemetry(payload, { topic, deviceId: topicInfo.deviceId });
      const inserted = saveTelemetry(record);
      console.log(`[telemetry] device=${record.device_id} seq=${record.seq} inserted=${inserted}`);
      return;
    }
    touchDevice(topicInfo.deviceId, payload, topicInfo.messageType);
    if (topicInfo.messageType === "cmd_ack") {
      handleCommandAck(topicInfo.deviceId, payload);
    }
  } catch (error) {
    console.warn(`[mqtt] process failed topic=${topic}: ${error.message}`);
  }
}

const mqttUrl = `${config.mqtt.protocol}://${config.mqtt.host}:${config.mqtt.port}`;
const mqttClient = mqtt.connect(mqttUrl, {
  clientId: config.mqtt.clientId,
  username: config.mqtt.username || undefined,
  password: config.mqtt.password || undefined,
  clean: false,
  keepalive: 30,
  connectTimeout: 10000,
  reconnectPeriod: 0,
  rejectUnauthorized: config.mqtt.rejectUnauthorized !== false,
  resubscribe: true,
  queueQoSZero: false
});

let mqttConnected = false;
let reconnectAttempt = 0;
let reconnectTimer = null;
let shuttingDown = false;
const publishingCommands = new Set();

function scheduleReconnect() {
  if (shuttingDown || reconnectTimer || mqttConnected) {
    return;
  }
  const base = Math.min(30000, 1000 * (2 ** Math.min(reconnectAttempt, 5)));
  const jitter = Math.trunc(base * 0.2 * (Math.random() * 2 - 1));
  const delay = Math.max(500, base + jitter);
  reconnectAttempt += 1;
  console.warn(`[mqtt] ${delay} ms 后执行第 ${reconnectAttempt} 次重连`);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    mqttClient.reconnect();
  }, delay);
}

const subscribedTypes = ["sensor", "status", "heartbeat", "error", "cmd_ack"];
const subscribedTopics = subscribedTypes.map(
  (type) => `${String(config.mqtt.topicRoot).replace(/\/+$/, "")}/+/${type}`
);

mqttClient.on("connect", () => {
  mqttConnected = true;
  reconnectAttempt = 0;
  console.log(`[mqtt] connected: ${mqttUrl}`);
  mqttClient.subscribe(
    subscribedTopics.map((topic) => ({ topic, qos: 1 })),
    (error) => {
      if (error) {
        console.error(`[mqtt] subscribe failed: ${error.message}`);
      } else {
        console.log(`[mqtt] subscribed: ${subscribedTopics.join(", ")}`);
      }
    }
  );
  dispatchPendingCommands();
});

mqttClient.on("message", handleMqttMessage);
mqttClient.on("close", () => {
  mqttConnected = false;
  scheduleReconnect();
});
mqttClient.on("offline", () => {
  mqttConnected = false;
});
mqttClient.on("error", (error) => {
  console.warn(`[mqtt] ${error.message}`);
});

function commandTopic(deviceId) {
  return `${String(config.mqtt.topicRoot).replace(/\/+$/, "")}/${deviceId}/cmd`;
}

function dispatchPendingCommands() {
  if (!mqttConnected || shuttingDown) {
    return;
  }
  const now = Date.now();
  statements.markTimedOut.run(now);
  for (const command of statements.nextPendingCommands.all(now)) {
    if (publishingCommands.has(command.cmd_id)) {
      continue;
    }
    publishingCommands.add(command.cmd_id);
    mqttClient.publish(
      commandTopic(command.device_id),
      command.payload_json,
      { qos: 1, retain: false },
      (error) => {
        publishingCommands.delete(command.cmd_id);
        if (error) {
          statements.markPublishFailed.run(error.message, command.cmd_id);
          console.warn(`[command] publish failed cmd_id=${command.cmd_id}: ${error.message}`);
          return;
        }
        statements.markPublished.run(nowIso(), command.cmd_id);
        console.log(`[command] PUBACK cmd_id=${command.cmd_id} device=${command.device_id}`);
      }
    );
  }
}

function createCommand(deviceId, input) {
  const allowedTargets = ["led", "buzzer", "relay"];
  const control = input.payload && typeof input.payload === "object" ? input.payload : input;
  const normalizedControl = {};
  for (const target of allowedTargets) {
    if (Object.prototype.hasOwnProperty.call(control, target)) {
      const value = control[target];
      if (![true, false, 0, 1].includes(value)) {
        throw new Error(`${target} must be boolean, 0 or 1`);
      }
      normalizedControl[target] = value === true || value === 1 ? 1 : 0;
    }
  }
  if (Object.keys(normalizedControl).length === 0) {
    throw new Error("command must include led, buzzer or relay");
  }

  const cmdId = crypto.randomUUID();
  const createdAt = Date.now();
  const ttlMs = Math.max(1000, Math.min(300000, finiteNumber(input.ttl_ms, config.server.commandTimeoutMs)));
  const expiresAt = createdAt + ttlMs;
  const envelope = {
    schema: 1,
    cmd_id: cmdId,
    type: "control",
    created_at: createdAt,
    expires_at: expiresAt,
    payload: normalizedControl
  };
  statements.insertCommand.run(
    cmdId,
    deviceId,
    "control",
    JSON.stringify(envelope),
    new Date(createdAt).toISOString(),
    expiresAt
  );
  dispatchPendingCommands();
  return { cmd_id: cmdId, device_id: deviceId, status: "PENDING", envelope };
}

function sendJson(res, statusCode, body) {
  const payload = statusCode === 204 ? "" : JSON.stringify(body);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS"
  });
  res.end(payload);
}

function readBody(req, limit = 1024 * 1024) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.setEncoding("utf8");
    req.on("data", (chunk) => {
      body += chunk;
      if (Buffer.byteLength(body, "utf8") > limit) {
        reject(new Error("request body too large"));
        req.destroy();
      }
    });
    req.on("end", () => resolve(body));
    req.on("error", reject);
  });
}

function telemetryRow(row) {
  if (!row) {
    return null;
  }
  return {
    ...row,
    replayed: Boolean(row.replayed),
    edge: {
      temperature_ema: row.temperature_ema,
      humidity_ema: row.humidity_ema,
      light_ema: row.light_ema,
      anomaly_flags: row.edge_anomaly_flags
    }
  };
}

function queryTelemetry(deviceId, limit) {
  const params = [];
  let where = "";
  if (deviceId) {
    where = "WHERE device_id = ?";
    params.push(deviceId);
  }
  params.push(limit);
  return database.prepare(`
    SELECT id, device_id, boot_id, seq, timestamp_ms, received_at, replayed,
           temperature, humidity, light, temperature_ema, humidity_ema, light_ema,
           edge_anomaly_flags, error_code
    FROM telemetry ${where}
    ORDER BY timestamp_ms DESC, id DESC
    LIMIT ?
  `).all(...params).map(telemetryRow).reverse();
}

function serveStatic(req, res) {
  const pathname = new URL(req.url, "http://localhost").pathname;
  const requested = pathname === "/" ? "index.html" : pathname.replace(/^\/+/, "");
  const publicRoot = path.resolve(ROOT, "public");
  const file = path.resolve(publicRoot, decodeURIComponent(requested));
  if (file !== publicRoot && !file.startsWith(`${publicRoot}${path.sep}`)) {
    sendJson(res, 403, { ok: false, error: "forbidden" });
    return;
  }
  if (!fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    sendJson(res, 404, { ok: false, error: "not found" });
    return;
  }
  const type = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8"
  }[path.extname(file).toLowerCase()] || "application/octet-stream";
  res.writeHead(200, { "Content-Type": type, "Cache-Control": "no-store" });
  fs.createReadStream(file).pipe(res);
}

async function handleRequest(req, res) {
  if (req.method === "OPTIONS") {
    sendJson(res, 204, {});
    return;
  }
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);
  const segments = url.pathname.split("/").filter(Boolean).map(decodeURIComponent);

  if (req.method === "POST" && url.pathname === "/api/iot/sensor") {
    try {
      const input = JSON.parse((await readBody(req)) || "{}");
      const record = normalizeTelemetry(input.payload, {
        topic: input.topic,
        clientId: input.clientid || input.clientId,
        timestamp: input.timestamp
      });
      const inserted = saveTelemetry(record);
      sendJson(res, 200, { ok: true, inserted, device_id: record.device_id, seq: record.seq });
    } catch (error) {
      sendJson(res, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (req.method === "GET" && url.pathname === "/api/sensor/latest") {
    const deviceId = url.searchParams.get("device_id");
    const rows = queryTelemetry(deviceId, 1);
    sendJson(res, 200, { ok: true, data: rows.at(-1) || null });
    return;
  }

  if (req.method === "GET" && url.pathname === "/api/sensor/history") {
    const limit = Math.max(1, Math.min(500, finiteNumber(url.searchParams.get("limit"), 50)));
    sendJson(res, 200, {
      ok: true,
      data: queryTelemetry(url.searchParams.get("device_id"), limit)
    });
    return;
  }

  if (req.method === "GET" && url.pathname === "/api/devices") {
    const devices = database.prepare(`
      SELECT device_id, online, first_seen, last_seen, firmware, state
      FROM devices ORDER BY last_seen DESC
    `).all().map((item) => ({ ...item, online: Boolean(item.online) }));
    sendJson(res, 200, { ok: true, data: devices });
    return;
  }

  if (segments[0] === "api" && segments[1] === "devices" && segments[2]) {
    const deviceId = segments[2];
    if (req.method === "GET" && segments.length === 3) {
      const device = database.prepare("SELECT * FROM devices WHERE device_id = ?").get(deviceId);
      if (!device) {
        sendJson(res, 404, { ok: false, error: "device not found" });
      } else {
        sendJson(res, 200, { ok: true, data: { ...device, online: Boolean(device.online) } });
      }
      return;
    }
    if (req.method === "GET" && segments[3] === "telemetry") {
      const limit = Math.max(1, Math.min(1000, finiteNumber(url.searchParams.get("limit"), 100)));
      sendJson(res, 200, { ok: true, data: queryTelemetry(deviceId, limit) });
      return;
    }
    if (req.method === "GET" && segments[3] === "commands") {
      const limit = Math.max(1, Math.min(200, finiteNumber(url.searchParams.get("limit"), 50)));
      const commands = database.prepare(`
        SELECT * FROM commands WHERE device_id = ? ORDER BY created_at DESC LIMIT ?
      `).all(deviceId, limit);
      sendJson(res, 200, { ok: true, data: commands });
      return;
    }
    if (req.method === "POST" && segments[3] === "commands") {
      try {
        const input = JSON.parse((await readBody(req, 8192)) || "{}");
        sendJson(res, 202, { ok: true, ...createCommand(deviceId, input) });
      } catch (error) {
        sendJson(res, 400, { ok: false, error: error.message });
      }
      return;
    }
  }

  if (req.method === "GET" && segments[0] === "api" && segments[1] === "commands" && segments[2]) {
    const command = database.prepare("SELECT * FROM commands WHERE cmd_id = ?").get(segments[2]);
    if (!command) {
      sendJson(res, 404, { ok: false, error: "command not found" });
    } else {
      sendJson(res, 200, { ok: true, data: command });
    }
    return;
  }

  if (req.method === "POST" && url.pathname === "/api/cmd") {
    try {
      const input = JSON.parse((await readBody(req, 8192)) || "{}");
      const deviceId = String(input.device_id || config.mqtt.defaultDeviceId).trim();
      sendJson(res, 202, { ok: true, ...createCommand(deviceId, input) });
    } catch (error) {
      sendJson(res, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (req.method === "GET" && url.pathname === "/api/status") {
    const latest = queryTelemetry(null, 1).at(-1) || null;
    const deviceCount = database.prepare("SELECT COUNT(*) AS count FROM devices").get().count;
    const command = database.prepare("SELECT * FROM commands ORDER BY created_at DESC LIMIT 1").get();
    sendJson(res, 200, {
      ok: true,
      latest,
      points: database.prepare("SELECT COUNT(*) AS count FROM telemetry").get().count,
      devices: deviceCount,
      lastCommand: command || null,
      mqtt: {
        connected: mqttConnected,
        host: config.mqtt.host,
        port: config.mqtt.port,
        topicRoot: config.mqtt.topicRoot
      }
    });
    return;
  }

  if (req.method === "GET") {
    serveStatic(req, res);
    return;
  }
  sendJson(res, 405, { ok: false, error: "method not allowed" });
}

const server = http.createServer((req, res) => {
  handleRequest(req, res).catch((error) => {
    console.error(error);
    if (!res.headersSent) {
      sendJson(res, 500, { ok: false, error: "internal server error" });
    } else {
      res.destroy();
    }
  });
});

const commandTimer = setInterval(() => {
  statements.markTimedOut.run(Date.now());
  dispatchPendingCommands();
}, 1000);
commandTimer.unref();

function shutdown(signal) {
  if (shuttingDown) {
    return;
  }
  shuttingDown = true;
  console.log(`[server] received ${signal}, shutting down`);
  clearInterval(commandTimer);
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
  }
  server.close(() => {
    mqttClient.end(true, {}, () => {
      database.close();
      process.exit(0);
    });
  });
}

process.on("SIGINT", () => shutdown("SIGINT"));
process.on("SIGTERM", () => shutdown("SIGTERM"));

server.listen(config.server.port, config.server.host, () => {
  console.log(`IoT backend running at http://localhost:${config.server.port}`);
  console.log(`SQLite database: ${DATABASE_FILE}`);
});

module.exports = {
  normalizeTelemetry,
  parseTopic,
  createCommand,
  handleMqttMessage
};
