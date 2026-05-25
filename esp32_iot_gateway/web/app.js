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
  firmware: "v0.1.0-prep",
  device_state: "INIT"
};

const labels = {
  led: "状态指示灯",
  buzzer: "蜂鸣器",
  relay: "继电器"
};

const history = [];
const maxHistory = 12;
const runtime = {
  demoMode: true,
  lastDemoLog: ""
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
    return "优秀";
  }
  if (score === 2) {
    return "良好";
  }
  return "一般";
}

function getNetworkQuality() {
  if (state.mqtt) {
    return "稳定在线";
  }
  if (state.wifi) {
    return "链路建立中";
  }
  return "等待联网";
}

function getControlSummary() {
  const active = Object.keys(labels).filter((key) => state[key]);
  if (!active.length) {
    return "当前未启用执行器";
  }
  return `已启用 ${active.length} 个执行器：${active.map((key) => labels[key]).join("、")}`;
}

function formatUptimeDetail() {
  if (state.uptime < 60) {
    return "系统刚启动";
  }
  if (state.uptime < 300) {
    return "运行稳定，处于预热阶段";
  }
  return "运行稳定，已进入持续监测";
}

function pushLog(title, detail) {
  const log = document.getElementById("event-log");
  const item = document.createElement("li");
  item.innerHTML = `<strong>${title}</strong><span>${nowText()} · ${detail}</span>`;
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

function updateDemoSensorData() {
  state.uptime += 2;
  state.temperature = Number((26.5 + Math.sin(state.uptime / 12) * 0.8).toFixed(1));
  state.humidity = Number((60.2 + Math.cos(state.uptime / 10) * 1.2).toFixed(1));
  state.light = Math.round(380 + Math.sin(state.uptime / 8) * 35);
  state.wifi = state.uptime >= 4 ? 1 : 0;
  state.mqtt = state.uptime >= 8 ? 1 : 0;
  state.device_state = state.mqtt ? "ONLINE" : state.wifi ? "MQTT_CONNECTING" : "WIFI_CONNECTING";
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
  state.firmware = payload.firmware ?? state.firmware;
  state.device_state = payload.state ?? payload.device_state ?? state.device_state;
}

async function fetchStatusFromApi() {
  const response = await fetch("/api/status", {
    cache: "no-store"
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const payload = await response.json();
  applyStatusPayload(payload);
  snapshotHistory();
}

async function sendControlToApi(target, value) {
  const response = await fetch("/api/control", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ [target]: value ? 1 : 0 })
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
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
    pushLog("切换到本地演示模式", reason);
    runtime.lastDemoLog = reason;
  }
  runtime.demoMode = true;
}

async function refreshRuntimeStatus() {
  try {
    await fetchStatusFromApi();
    if (runtime.demoMode) {
      pushLog("已连接真实设备接口", "前端开始读取 /api/status");
    }
    runtime.demoMode = false;
    runtime.lastDemoLog = "";
  } catch (error) {
    enterDemoMode("未检测到可用的 /api/status，继续展示本地演示数据");
    updateDemoSensorData();
  }

  if (state.uptime === 4 && runtime.demoMode) {
    pushLog("Wi-Fi 已连接", "Station 获取连接状态");
  }
  if (state.uptime === 8 && runtime.demoMode) {
    pushLog("MQTT 已连接", "设备状态切换为 ONLINE");
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
  ctx.fillText("温度", padding, 22);
  ctx.fillText("湿度", padding + 52, 22);
  ctx.fillText("光照", padding + 104, 22);

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
    status.textContent = enabled ? "开启" : "关闭";
    button.setAttribute("aria-pressed", String(enabled));
  });
}

function render() {
  const wifiConnected = state.wifi ? "已连接" : "未连接";
  const mqttConnected = state.mqtt ? "已连接" : "未连接";
  const activeControlCount = Object.keys(labels).filter((key) => state[key]).length;

  document.getElementById("temperature").textContent = state.temperature;
  document.getElementById("humidity").textContent = state.humidity;
  document.getElementById("light").textContent = state.light;
  document.getElementById("wifi").textContent = wifiConnected;
  document.getElementById("mqtt").textContent = mqttConnected;
  document.getElementById("uptime").textContent = `${state.uptime} s`;
  document.getElementById("error-code").textContent = state.error_code;
  document.getElementById("firmware").textContent = state.firmware;
  document.getElementById("wifi-detail").textContent = state.wifi ? "STA 已获取链路" : "等待接入";
  document.getElementById("mqtt-detail").textContent = state.mqtt ? "Broker 会话正常" : state.wifi ? "正在建立会话" : "等待服务端";
  document.getElementById("uptime-detail").textContent = formatUptimeDetail();
  document.getElementById("active-controls").textContent = `${activeControlCount} / 3`;
  document.getElementById("comfort-score").textContent = getComfortScore();
  document.getElementById("network-quality").textContent = getNetworkQuality();
  document.getElementById("control-summary").textContent = getControlSummary();
  document.getElementById("temperature-avg").textContent = `${average("temperature").toFixed(1)} °C`;
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

    button.disabled = true;

    if (!runtime.demoMode) {
      try {
        await sendControlToApi(target, nextValue);
        pushLog(`${labels[target]}${nextValue ? "开启" : "关闭"}`, "控制命令已发送到 /api/control");
        state[target] = nextValue;
        render();

        const synced = await syncControlState(target, nextValue);
        runtime.demoMode = false;
        runtime.lastDemoLog = "";
        render();
        if (!synced) {
          pushLog("状态回读延迟", `${labels[target]}命令已发送，等待设备回传最新状态`);
        }
        return;
      } catch (error) {
        enterDemoMode("控制接口不可用，已回退到本地演示状态");
      } finally {
        button.disabled = false;
      }
    }

    state[target] = nextValue;
    pushLog(`${labels[target]}${state[target] ? "开启" : "关闭"}`, "控制状态已写入本地演示状态");
    render();
    button.disabled = false;
  });
});

document.getElementById("copy-payload").addEventListener("click", async () => {
  const payload = document.getElementById("payload").textContent;
  try {
    await navigator.clipboard.writeText(payload);
    pushLog("接口快照已复制", "JSON payload 已复制到剪贴板");
  } catch (error) {
    pushLog("复制失败", "当前浏览器不允许访问剪贴板");
  }
});

document.getElementById("clear-log").addEventListener("click", () => {
  document.getElementById("event-log").replaceChildren();
});

snapshotHistory();
pushLog("控制台启动", "加载静态演示数据");
refreshRuntimeStatus();
setInterval(() => {
  refreshRuntimeStatus();
}, 2000);
