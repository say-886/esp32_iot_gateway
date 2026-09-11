"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");

async function main() {
  const commandSecret = "0123456789abcdef0123456789abcdef";
  const deviceToken = "device-http-token-0123456789";
  const device = http.createServer((req, res) => {
    assert.equal(req.url, "/api/control");
    assert.equal(req.headers.authorization, `Bearer ${deviceToken}`);
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      const envelope = JSON.parse(body);
      const canonical = [
        "v1", envelope.device_id, envelope.cmd_id, envelope.type,
        String(envelope.created_at), String(envelope.expires_at),
        String(envelope.payload.led ?? "-"), String(envelope.payload.buzzer ?? "-"),
        String(envelope.payload.relay ?? "-")
      ].join("\n");
      const expected = crypto.createHmac("sha256", commandSecret).update(canonical).digest("hex");
      assert.equal(envelope.auth, expected);
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ ok: true, cmd_id: envelope.cmd_id, status: "executed", code: 0 }));
    });
  });
  await new Promise((resolve) => device.listen(0, "127.0.0.1", resolve));

  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "iot-http-command-"));
  const configFile = path.join(tempDir, "config.json");
  fs.writeFileSync(configFile, JSON.stringify({
    server: { host: "127.0.0.1", port: 0, commandTimeoutMs: 30000 },
    mqtt: {
      protocol: "mqtt", host: "127.0.0.1", port: 1, clientId: "http-command-test",
      username: "", password: "", topicRoot: "esp32/gateway", defaultDeviceId: "esp32_gateway_001",
      commandSecret, httpUrl: `http://127.0.0.1:${device.address().port}`,
      httpApiToken: deviceToken, devices: {}
    }
  }));
  process.env.IOT_CONFIG_FILE = configFile;
  process.env.IOT_DATA_DIR = tempDir;
  process.env.IOT_OPERATOR_API_KEY = "operator-test-key-0123456789";
  const { server, shutdownForTest } = require("./server");
  if (!server.listening) await new Promise((resolve) => server.once("listening", resolve));
  const address = server.address();
  const response = await fetch(`http://127.0.0.1:${address.port}/api/cmd`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-api-key": process.env.IOT_OPERATOR_API_KEY
    },
    body: JSON.stringify({ transport: "http", device_id: "esp32_gateway_001", led: 1 })
  });
  assert.equal(response.status, 202);
  const accepted = await response.json();
  assert.equal(accepted.transport, "http");

  let status;
  for (let i = 0; i < 20; i += 1) {
    await new Promise((resolve) => setTimeout(resolve, 25));
    const statusResponse = await fetch(`http://127.0.0.1:${address.port}/api/commands/${accepted.cmd_id}`);
    status = (await statusResponse.json()).data;
    if (status.status === "ACKED") break;
  }
  assert.equal(status.status, "ACKED");
  assert.equal(status.transport, "http");
  shutdownForTest();
  await new Promise((resolve) => device.close(resolve));
  console.log(JSON.stringify({ ok: true, status: status.status, transport: status.transport }));
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
