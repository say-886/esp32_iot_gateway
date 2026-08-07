const state = {
  temperature: 26.5,
  humidity: 60.2,
  light: 380,
  led: 0,
  buzzer: 0,
  relay: 0,
  wifi: 0,
  mqtt: 0,
  uptime: 0,
  error_code: 0,
  error_flags: 0,
  firmware: "v0.1.0-prep",
  device_state: "INIT",
  device_id: "esp32_gateway_001",
  mqtt_host: "",
  mqtt_port: 8883,
  mqtt_use_tls: 1,
  mqtt_ws_port: 8084,
  mqtt_ws_path: "/mqtt",
  mqtt_status_topic: "",
  mqtt_sensor_topic: "",
  mqtt_heartbeat_topic: "",
  mqtt_error_topic: "",
  mqtt_cmd_topic: ""
};

const labels = {
  led: "Status Lamp",
  buzzer: "Buzzer",
  relay: "Relay"
};

const history = [];
const maxHistory = 12;
const decoder = new TextDecoder();
const runtime = {
  demoMode: true,
  lastDemoLog: "",
  lastTransportLog: "",
  lastStateUpdateAt: 0,
  apiToken: window.localStorage.getItem("gateway_api_token") || "",
  viewerHost: window.localStorage.getItem("mqtt_viewer_host") || "",
  viewerUsername: window.localStorage.getItem("mqtt_viewer_username") || "",
  viewerClient: null,
  viewerConnected: false,
  viewerConnecting: false
};

function delay(ms) {
  return new Promise((resolve) => {
    window.setTimeout(resolve, ms);
  });
}

function nowText() {
  return new Date().toLocaleTimeString("zh-CN", { hour12: false });
}

function average(key) {
  if (!history.length) {
    return 0;
  }
  const total = history.reduce((sum, point) => sum + point[key], 0);
  return total / history.length;
}

function getComfortScore() {
  const tempOk = state.temperature >= 24 && state.temperature <= 28;
  const humidityOk = state.humidity >= 45 && state.humidity <= 65;
  const lightOk = state.light >= 320 && state.light <= 450;
  const score = [tempOk, humidityOk, lightOk].filter(Boolean).length;
  if (score === 3) {
    return "Excellent";
  }
  if (score === 2) {
    return "Good";
  }
  return "Watch";
}

function getNetworkQuality() {
  if (state.mqtt) {
    return "Broker Online";
  }
  if (state.wifi) {
    return "Wi-Fi Only";
  }
  return "Offline";
}

function getControlSummary() {
  const active = Object.keys(labels).filter((key) => state[key]);
  if (!active.length) {
    return "No actuator is enabled";
  }
  return `${active.length} actuator(s) enabled: ${active.map((key) => labels[key]).join(", ")}`;
}

function formatUptimeDetail() {
  if (state.uptime < 60) {
    return "System warm-up window";
  }
  if (state.uptime < 300) {
    return "Telemetry has stabilized";
  }
  return "Long-running session";
}

function pushLog(title, detail) {
  const log = document.getElementById("event-log");
  const item = document.createElement("li");
  item.innerHTML = `<strong>${title}</strong><span>${nowText()} | ${detail}</span>`;
  log.prepend(item);

  while (log.children.length > 8) {
    log.removeChild(log.lastElementChild);
  }
}

function snapshotHistory() {
  history.push({
    temperature: state.temperature,
    humidity: state.humidity,
    light: state.light
  });

  if (history.length > maxHistory) {
    history.shift();
  }
}

function markLiveStateUpdate() {
  runtime.lastStateUpdateAt = Date.now();
}

function buildTopic(suffix) {
  return `esp32/gateway/${state.device_id}/${suffix}`;
}

function ensureTopicMeta() {
  state.mqtt_ws_port = Number(state.mqtt_ws_port || 8084);
  state.mqtt_ws_path = state.mqtt_ws_path || "/mqtt";
  state.mqtt_status_topic = state.mqtt_status_topic || buildTopic("status");
  state.mqtt_sensor_topic = state.mqtt_sensor_topic || buildTopic("sensor");
  state.mqtt_heartbeat_topic = state.mqtt_heartbeat_topic || buildTopic("heartbeat");
  state.mqtt_error_topic = state.mqtt_error_topic || buildTopic("error");
  state.mqtt_cmd_topic = state.mqtt_cmd_topic || buildTopic("cmd");
}

function updateDemoSensorData() {
  state.uptime += 2;
  state.temperature = Number((26.5 + Math.sin(state.uptime / 12) * 0.8).toFixed(1));
  state.humidity = Number((60.2 + Math.cos(state.uptime / 10) * 1.2).toFixed(1));
  state.light = Math.round(380 + Math.sin(state.uptime / 8) * 35);
  state.wifi = state.uptime >= 4 ? 1 : 0;
  state.mqtt = state.uptime >= 8 ? 1 : 0;
  state.device_state = state.mqtt ? "ONLINE" : state.wifi ? "MQTT_CONNECTING" : "WIFI_CONNECTING";
  ensureTopicMeta();
  snapshotHistory();
}

function applyStatusPayload(payload) {
  state.temperature = Number(payload.temperature ?? state.temperature);
  state.humidity = Number(payload.humidity ?? state.humidity);
  state.light = Number(payload.light ?? state.light);
  state.led = Number(payload.led ?? state.led);
  state.buzzer = Number(payload.buzzer ?? state.buzzer);
  state.relay = Number(payload.relay ?? state.relay);
  state.wifi = Number(payload.wifi ?? state.wifi);
  state.mqtt = Number(payload.mqtt ?? state.mqtt);
  state.uptime = Number(payload.uptime ?? state.uptime);
  state.error_code = Number(payload.error_code ?? state.error_code);
  state.error_flags = Number(payload.error_flags ?? state.error_flags);
  state.firmware = payload.firmware ?? state.firmware;
  state.device_state = payload.state ?? payload.device_state ?? state.device_state;
  state.device_id = payload.device_id ?? state.device_id;
  state.mqtt_host = payload.mqtt_host ?? state.mqtt_host;
  state.mqtt_port = Number(payload.mqtt_port ?? state.mqtt_port);
  state.mqtt_use_tls = Number(payload.mqtt_use_tls ?? state.mqtt_use_tls);
  state.mqtt_ws_port = Number(payload.mqtt_ws_port ?? state.mqtt_ws_port);
  state.mqtt_ws_path = payload.mqtt_ws_path ?? state.mqtt_ws_path;
  state.mqtt_status_topic = payload.mqtt_status_topic ?? state.mqtt_status_topic;
  state.mqtt_sensor_topic = payload.mqtt_sensor_topic ?? state.mqtt_sensor_topic;
  state.mqtt_heartbeat_topic = payload.mqtt_heartbeat_topic ?? state.mqtt_heartbeat_topic;
  state.mqtt_error_topic = payload.mqtt_error_topic ?? state.mqtt_error_topic;
  state.mqtt_cmd_topic = payload.mqtt_cmd_topic ?? state.mqtt_cmd_topic;
  ensureTopicMeta();
  markLiveStateUpdate();
}

function applyMqttPayload(topic, payload) {
  if (topic === state.mqtt_status_topic) {
    applyStatusPayload(payload);
    snapshotHistory();
    return;
  }

  if (topic === state.mqtt_sensor_topic) {
    state.temperature = Number(payload.temperature ?? state.temperature);
    state.humidity = Number(payload.humidity ?? state.humidity);
    state.light = Number(payload.light ?? state.light);
    snapshotHistory();
    return;
  }

  if (topic === state.mqtt_heartbeat_topic) {
    state.uptime = Number(payload.uptime ?? state.uptime);
    state.wifi = Number(payload.wifi ?? state.wifi);
    state.mqtt = Number(payload.mqtt ?? state.mqtt);
    state.device_state = payload.state ?? state.device_state;
    return;
  }

  if (topic === state.mqtt_error_topic) {
    state.error_code = Number(payload.error_code ?? state.error_code);
    state.error_flags = Number(payload.error_flags ?? state.error_flags);
    state.uptime = Number(payload.uptime ?? state.uptime);
  }
}

async function fetchStatusFromApi() {
  const response = await fetch("/api/status", { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const payload = await response.json();
  applyStatusPayload(payload);
  snapshotHistory();
}

async function sendControlToApi(target, value) {
  if (!runtime.apiToken) {
    runtime.apiToken = window.prompt("Enter device API token") || "";
    if (runtime.apiToken) {
      window.localStorage.setItem("gateway_api_token", runtime.apiToken);
    }
  }

  const response = await fetch("/api/control", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${runtime.apiToken}`
    },
    body: JSON.stringify({ [target]: value ? 1 : 0 })
  });

  if (!response.ok) {
    if (response.status === 401) {
      runtime.apiToken = "";
      window.localStorage.removeItem("gateway_api_token");
    }
    throw new Error(`HTTP ${response.status}`);
  }
}

function canPublishMqttControl() {
  return Boolean(runtime.viewerClient && runtime.viewerConnected && state.mqtt_cmd_topic);
}

function publishControlToMqtt(target, value) {
  return new Promise((resolve, reject) => {
    if (!canPublishMqttControl()) {
      reject(new Error("MQTT control channel is not ready"));
      return;
    }

    const payload = JSON.stringify({ [target]: value ? 1 : 0 });
    runtime.viewerClient.publish(
      state.mqtt_cmd_topic,
      payload,
      { qos: 1, retain: false },
      (error) => {
        if (error) {
          reject(error);
          return;
        }
        resolve(payload);
      }
    );
  });
}

async function waitForControlEcho(target, expectedValue, issuedAt) {
  const retryIntervals = [180, 250, 350, 500];

  for (const waitMs of retryIntervals) {
    await delay(waitMs);
    if (runtime.lastStateUpdateAt > issuedAt && Number(state[target]) === Number(expectedValue)) {
      return true;
    }

    try {
      await fetchStatusFromApi();
    } catch (error) {
      // MQTT-only remote mode can work without /api/status.
    }

    if (runtime.lastStateUpdateAt > issuedAt && Number(state[target]) === Number(expectedValue)) {
      return true;
    }
  }

  return false;
}

async function syncControlState(target, expectedValue) {
  const retryIntervals = [180, 250, 350];

  for (const waitMs of retryIntervals) {
    await delay(waitMs);
    await fetchStatusFromApi();
    if (Number(state[target]) === Number(expectedValue)) {
      return true;
    }
  }

  return false;
}

function enterDemoMode(reason) {
  if (runtime.lastDemoLog !== reason) {
    pushLog("Demo mode", reason);
    runtime.lastDemoLog = reason;
  }
  runtime.demoMode = true;
}

function getViewerTopics() {
  ensureTopicMeta();
  return [
    state.mqtt_status_topic,
    state.mqtt_sensor_topic,
    state.mqtt_heartbeat_topic,
    state.mqtt_error_topic
  ];
}

function decodePayload(payload) {
  if (typeof payload === "string") {
    return payload;
  }
  if (payload instanceof Uint8Array) {
    return decoder.decode(payload);
  }
  return `${payload}`;
}

function getViewerField(id) {
  const element = document.getElementById(id);
  return element ? element.value.trim() : "";
}

function buildViewerUrl(host) {
  return `wss://${host}:${Number(state.mqtt_ws_port || 8084)}${state.mqtt_ws_path || "/mqtt"}`;
}

function updateViewerInputs() {
  const hostInput = document.getElementById("mqtt-viewer-host");
  const userInput = document.getElementById("mqtt-viewer-username");

  if (hostInput && !hostInput.value) {
    hostInput.value = runtime.viewerHost || state.mqtt_host;
  }
  if (userInput && !userInput.value) {
    userInput.value = runtime.viewerUsername;
  }
}

function renderTopicList() {
  const list = document.getElementById("mqtt-topic-list");
  if (!list) {
    return;
  }

  list.replaceChildren();
  for (const topic of getViewerTopics()) {
    const item = document.createElement("li");
    item.textContent = topic;
    list.appendChild(item);
  }
}

function renderViewerState() {
  updateViewerInputs();
  renderTopicList();

  const statusElement = document.getElementById("mqtt-viewer-status");
  const detailElement = document.getElementById("mqtt-viewer-detail");
  const connectButton = document.getElementById("mqtt-viewer-connect");
  const disconnectButton = document.getElementById("mqtt-viewer-disconnect");
  const host = getViewerField("mqtt-viewer-host") || runtime.viewerHost || state.mqtt_host || "host";
  const detail = buildViewerUrl(host);

  if (detailElement) {
    detailElement.textContent = detail;
  }

  if (statusElement) {
    if (runtime.viewerConnecting) {
      statusElement.textContent = "Connecting";
    } else if (runtime.viewerConnected) {
      statusElement.textContent = "Connected";
    } else {
      statusElement.textContent = "Idle";
    }
  }

  if (connectButton) {
    connectButton.disabled = runtime.viewerConnecting || runtime.viewerConnected;
  }
  if (disconnectButton) {
    disconnectButton.disabled = !runtime.viewerClient && !runtime.viewerConnecting && !runtime.viewerConnected;
  }
}

function cleanupViewerClient(silent) {
  if (runtime.viewerClient) {
    runtime.viewerClient.end(true);
    runtime.viewerClient = null;
  }
  runtime.viewerConnected = false;
  runtime.viewerConnecting = false;
  renderViewerState();
  if (!silent) {
    pushLog("EMQX viewer", "Disconnected");
  }
}

function handleViewerMessage(topic, rawPayload) {
  let payload = null;
  const text = decodePayload(rawPayload);

  try {
    payload = JSON.parse(text);
  } catch (error) {
    pushLog("MQTT parse error", `Topic ${topic} is not valid JSON`);
    return;
  }

  runtime.demoMode = false;
  runtime.lastDemoLog = "";
  runtime.lastTransportLog = "";
  applyMqttPayload(topic, payload);
  render();
}

function connectViewer() {
  const host = getViewerField("mqtt-viewer-host") || state.mqtt_host;
  const username = getViewerField("mqtt-viewer-username");
  const password = getViewerField("mqtt-viewer-password");

  if (!window.mqtt || typeof window.mqtt.connect !== "function") {
    pushLog("MQTT.js missing", "Browser could not load the MQTT over WSS client");
    return;
  }

  if (!host || !username || !password) {
    pushLog("Viewer config", "Host, username, and password are required");
    return;
  }

  runtime.viewerHost = host;
  runtime.viewerUsername = username;
  window.localStorage.setItem("mqtt_viewer_host", host);
  window.localStorage.setItem("mqtt_viewer_username", username);

  if (runtime.viewerClient) {
    cleanupViewerClient(true);
  }

  const url = buildViewerUrl(host);
  const clientId = `web_${state.device_id}_${Date.now()}`;
  runtime.viewerConnecting = true;
  renderViewerState();

  const client = window.mqtt.connect(url, {
    username,
    password,
    clientId,
    clean: true,
    reconnectPeriod: 2000,
    connectTimeout: 10000
  });
  runtime.viewerClient = client;

  client.on("connect", () => {
    runtime.viewerConnecting = false;
    runtime.viewerConnected = true;
    renderViewerState();
    pushLog("EMQX viewer", `Connected to ${url}`);

    client.subscribe(getViewerTopics(), { qos: 1 }, (error) => {
      if (error) {
        pushLog("Subscribe failed", error.message || "Unknown subscribe error");
      } else {
        pushLog("Topics subscribed", getViewerTopics().join(" | "));
      }
    });
  });

  client.on("message", handleViewerMessage);

  client.on("reconnect", () => {
    runtime.viewerConnecting = true;
    runtime.viewerConnected = false;
    renderViewerState();
  });

  client.on("close", () => {
    runtime.viewerConnecting = false;
    runtime.viewerConnected = false;
    renderViewerState();
  });

  client.on("error", (error) => {
    runtime.viewerConnecting = false;
    runtime.viewerConnected = false;
    renderViewerState();
    pushLog("MQTT viewer error", error.message || "Unknown broker error");
  });
}

async function refreshRuntimeStatus() {
  try {
    await fetchStatusFromApi();
    if (runtime.demoMode) {
      pushLog("HTTP online", "Dashboard is now reading live /api/status data");
    }
    runtime.demoMode = false;
    runtime.lastDemoLog = "";
    runtime.lastTransportLog = "";
  } catch (error) {
    if (runtime.viewerConnected || runtime.viewerConnecting) {
      runtime.demoMode = false;
      runtime.lastDemoLog = "";
      if (runtime.lastTransportLog !== "mqtt_only") {
        pushLog("MQTT live mode", "Using broker telemetry because /api/status is unavailable");
        runtime.lastTransportLog = "mqtt_only";
      }
    } else {
      runtime.lastTransportLog = "";
      enterDemoMode("Falling back to local demo telemetry because /api/status is unavailable");
      updateDemoSensorData();
    }
  }

  render();
}

function drawTrendChart() {
  const canvas = document.getElementById("trend-chart");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  const padding = 36;
  const chartWidth = width - padding * 2;
  const chartHeight = height - padding * 2;

  ctx.clearRect(0, 0, width, height);
  const bgGradient = ctx.createLinearGradient(0, 0, 0, height);
  bgGradient.addColorStop(0, "#f9fbff");
  bgGradient.addColorStop(1, "#edf5ff");
  ctx.fillStyle = bgGradient;
  ctx.fillRect(0, 0, width, height);

  ctx.strokeStyle = "#d9e2ec";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) {
    const y = padding + (chartHeight / 4) * i;
    ctx.beginPath();
    ctx.moveTo(padding, y);
    ctx.lineTo(width - padding, y);
    ctx.stroke();
  }

  ctx.strokeStyle = "rgba(148, 163, 184, 0.18)";
  for (let i = 0; i < maxHistory; i += 1) {
    const x = padding + (chartWidth / Math.max(maxHistory - 1, 1)) * i;
    ctx.beginPath();
    ctx.moveTo(x, padding);
    ctx.lineTo(x, height - padding);
    ctx.stroke();
  }

  const series = [
    { key: "temperature", color: "#b45309", min: 20, max: 32 },
    { key: "humidity", color: "#2563eb", min: 45, max: 75 },
    { key: "light", color: "#0f766e", min: 300, max: 460 }
  ];

  series.forEach((line) => {
    const points = history.map((point, index) => {
      const x = padding + (chartWidth / Math.max(maxHistory - 1, 1)) * index;
      const ratio = (point[line.key] - line.min) / (line.max - line.min);
      const y = padding + chartHeight - Math.max(0, Math.min(1, ratio)) * chartHeight;
      return { x, y };
    });

    if (!points.length) {
      return;
    }

    const areaGradient = ctx.createLinearGradient(0, padding, 0, height - padding);
    areaGradient.addColorStop(0, `${line.color}33`);
    areaGradient.addColorStop(1, `${line.color}05`);

    ctx.beginPath();
    ctx.moveTo(points[0].x, height - padding);
    points.forEach((point) => {
      ctx.lineTo(point.x, point.y);
    });
    ctx.lineTo(points[points.length - 1].x, height - padding);
    ctx.closePath();
    ctx.fillStyle = areaGradient;
    ctx.fill();

    ctx.strokeStyle = line.color;
    ctx.lineWidth = 3;
    ctx.beginPath();
    points.forEach((point, index) => {
      if (index === 0) {
        ctx.moveTo(point.x, point.y);
      } else {
        ctx.lineTo(point.x, point.y);
      }
    });
    ctx.stroke();

    const lastPoint = points[points.length - 1];
    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    ctx.arc(lastPoint.x, lastPoint.y, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 3;
    ctx.strokeStyle = line.color;
    ctx.stroke();
  });

  ctx.fillStyle = "#657387";
  ctx.font = "14px Microsoft YaHei, Segoe UI, Arial";
  ctx.fillText("Temp", padding, 22);
  ctx.fillText("Humidity", padding + 52, 22);
  ctx.fillText("Light", padding + 128, 22);

  ctx.fillStyle = "#94a3b8";
  ctx.font = "12px Microsoft YaHei, Segoe UI, Arial";
  for (let i = 0; i < history.length; i += 1) {
    const x = padding + (chartWidth / Math.max(maxHistory - 1, 1)) * i;
    ctx.fillText(`${(history.length - i) * 2}s`, x - 10, height - 12);
  }
}

function setMeter(id, value) {
  const meter = document.getElementById(id);
  meter.value = value;
}

function renderControls() {
  document.querySelectorAll(".control-button").forEach((button) => {
    const target = button.dataset.target;
    const enabled = Boolean(state[target]);
    const status = button.querySelector("small");
    button.classList.toggle("is-on", enabled);
    status.textContent = enabled ? "On" : "Off";
    button.setAttribute("aria-pressed", String(enabled));
  });
}

function render() {
  ensureTopicMeta();
  renderViewerState();

  const wifiConnected = state.wifi ? "Online" : "Offline";
  const mqttConnected = state.mqtt ? "Online" : "Offline";
  const activeControlCount = Object.keys(labels).filter((key) => state[key]).length;

  document.getElementById("temperature").textContent = state.temperature;
  document.getElementById("humidity").textContent = state.humidity;
  document.getElementById("light").textContent = state.light;
  document.getElementById("wifi").textContent = wifiConnected;
  document.getElementById("mqtt").textContent = mqttConnected;
  document.getElementById("uptime").textContent = `${state.uptime} s`;
  document.getElementById("error-code").textContent = state.error_code;
  document.getElementById("firmware").textContent = state.firmware;
  document.getElementById("device-id").textContent = state.device_id;
  document.getElementById("status-topic").textContent = state.mqtt_status_topic;
  document.getElementById("wifi-detail").textContent = state.wifi ? "Station link is ready" : "Waiting for Wi-Fi";
  document.getElementById("mqtt-detail").textContent = state.mqtt ? "Broker session is healthy" : state.wifi ? "Broker session is connecting" : "Waiting for broker";
  document.getElementById("uptime-detail").textContent = formatUptimeDetail();
  document.getElementById("active-controls").textContent = `${activeControlCount} / 3`;
  document.getElementById("comfort-score").textContent = getComfortScore();
  document.getElementById("network-quality").textContent = getNetworkQuality();
  document.getElementById("control-summary").textContent = getControlSummary();
  document.getElementById("temperature-avg").textContent = `${average("temperature").toFixed(1)} C`;
  document.getElementById("humidity-avg").textContent = `${average("humidity").toFixed(1)} %`;
  document.getElementById("light-avg").textContent = `${Math.round(average("light"))} lx`;

  const statePill = document.getElementById("device-state");
  statePill.textContent = state.device_state;
  statePill.classList.toggle("is-online", state.device_state === "ONLINE");

  document.getElementById("last-sync").textContent = nowText();
  document.getElementById("payload").textContent = JSON.stringify(state, null, 2);

  setMeter("temperature-meter", state.temperature);
  setMeter("humidity-meter", state.humidity);
  setMeter("light-meter", state.light);
  renderControls();
  drawTrendChart();
}

document.querySelectorAll(".control-button").forEach((button) => {
  button.addEventListener("click", async () => {
    const target = button.dataset.target;
    const nextValue = state[target] ? 0 : 1;
    const preferMqttControl = runtime.viewerConnected || runtime.viewerConnecting;

    button.disabled = true;
    try {
      if (canPublishMqttControl()) {
        try {
          const issuedAt = Date.now();
          const payload = await publishControlToMqtt(target, nextValue);
          pushLog(`${labels[target]} ${nextValue ? "on" : "off"}`,
            `MQTT command sent to ${state.mqtt_cmd_topic}: ${payload}`);
          runtime.demoMode = false;
          runtime.lastDemoLog = "";
          runtime.lastTransportLog = "";
          state[target] = nextValue;
          render();

          const synced = await waitForControlEcho(target, nextValue, issuedAt);
          runtime.demoMode = false;
          runtime.lastDemoLog = "";
          render();
          if (!synced) {
            pushLog("Control sync delay", `${labels[target]} request sent, waiting for device state echo`);
          }
          return;
        } catch (error) {
          pushLog("MQTT publish failed", error.message || "Could not publish the MQTT control command");
        }
      }

      if (!runtime.demoMode) {
        try {
          const issuedAt = Date.now();
          await sendControlToApi(target, nextValue);
          pushLog(`${labels[target]} ${nextValue ? "on" : "off"}`, "Control request sent to /api/control");
          state[target] = nextValue;
          render();

          const synced = await waitForControlEcho(target, nextValue, issuedAt);
          runtime.demoMode = false;
          runtime.lastDemoLog = "";
          render();
          if (!synced) {
            pushLog("Control sync delay", `${labels[target]} request sent, waiting for device state echo`);
          }
          return;
        } catch (error) {
          if (preferMqttControl) {
            pushLog("Control request failed", "MQTT publish failed and /api/control is unavailable");
            render();
            return;
          }
          enterDemoMode("Control API is unavailable, local demo state is active");
        }
      }

      state[target] = nextValue;
      pushLog(`${labels[target]} ${state[target] ? "on" : "off"}`, "Demo state updated locally");
      render();
    } finally {
      button.disabled = false;
    }
  });
});

document.getElementById("copy-payload").addEventListener("click", async () => {
  const payload = document.getElementById("payload").textContent;
  try {
    await navigator.clipboard.writeText(payload);
    pushLog("Payload copied", "Snapshot JSON copied to clipboard");
  } catch (error) {
    pushLog("Copy failed", "Clipboard access is not available");
  }
});

document.getElementById("clear-log").addEventListener("click", () => {
  document.getElementById("event-log").replaceChildren();
});

document.getElementById("mqtt-viewer-connect").addEventListener("click", () => {
  connectViewer();
});

document.getElementById("mqtt-viewer-disconnect").addEventListener("click", () => {
  cleanupViewerClient(false);
});

snapshotHistory();
ensureTopicMeta();
pushLog("Dashboard boot", "Loading local telemetry shell");
render();
refreshRuntimeStatus();
window.setInterval(() => {
  refreshRuntimeStatus();
}, 2000);
