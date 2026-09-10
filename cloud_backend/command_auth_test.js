"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");

function canonical(deviceId, cmdId, createdAt, expiresAt, control) {
  return [
    "v1",
    deviceId,
    cmdId,
    "control",
    String(createdAt),
    String(expiresAt),
    Object.hasOwn(control, "led") ? String(control.led) : "-",
    Object.hasOwn(control, "buzzer") ? String(control.buzzer) : "-",
    Object.hasOwn(control, "relay") ? String(control.relay) : "-"
  ].join("\n");
}

const secret = "0123456789abcdef0123456789abcdef";
const deviceId = "esp32_gateway_001";
const cmdId = "cmd-test-001";
const createdAt = 1786080000000;
const expiresAt = createdAt + 30000;
const control = { led: 1, relay: 0 };
const expectedCanonical =
  "v1\nesp32_gateway_001\ncmd-test-001\ncontrol\n1786080000000\n1786080030000\n1\n-\n0";
assert.equal(canonical(deviceId, cmdId, createdAt, expiresAt, control), expectedCanonical);

const expectedAuth = crypto
  .createHmac("sha256", secret)
  .update(expectedCanonical, "utf8")
  .digest("hex");
assert.equal(expectedAuth.length, 64);
assert.notEqual(expectedAuth, crypto
  .createHmac("sha256", secret)
  .update(`${expectedCanonical}\n`, "utf8")
  .digest("hex"));

console.log(JSON.stringify({ ok: true, canonical: expectedCanonical, auth: expectedAuth }));
