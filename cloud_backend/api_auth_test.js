"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "iot-api-auth-"));
process.env.IOT_DATA_DIR = tempDir;
process.env.IOT_COMMAND_SECRET = "0123456789abcdef0123456789abcdef";
process.env.IOT_OPERATOR_API_KEY = "operator-test-key-0123456789";

const { server, shutdownForTest } = require("./server");

async function request(pathname, options = {}) {
  const address = server.address();
  return fetch(`http://127.0.0.1:${address.port}${pathname}`, options);
}

async function main() {
  if (!server.listening) {
    await new Promise((resolve) => server.once("listening", resolve));
  }

  const denied = await request("/api/cmd", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ led: 1 })
  });
  assert.equal(denied.status, 401);

  const accepted = await request("/api/cmd", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-api-key": process.env.IOT_OPERATOR_API_KEY
    },
    body: JSON.stringify({ led: 1 })
  });
  assert.equal(accepted.status, 202);
  const command = await accepted.json();
  assert.equal(command.ok, true);

  const hiddenAudit = await request("/api/audit");
  assert.equal(hiddenAudit.status, 401);

  const auditResponse = await request("/api/audit?limit=10", {
    headers: { "x-api-key": process.env.IOT_OPERATOR_API_KEY }
  });
  assert.equal(auditResponse.status, 200);
  const audit = await auditResponse.json();
  assert.equal(audit.data.some((row) => row.outcome === "denied"), true);
  assert.equal(audit.data.some((row) => row.cmd_id === command.cmd_id && row.outcome === "accepted"), true);

  console.log(JSON.stringify({ ok: true, auditRows: audit.data.length }));
}

main()
  .finally(() => new Promise((resolve) => {
    shutdownForTest();
    server.once("close", resolve);
  }))
  .finally(() => {});
