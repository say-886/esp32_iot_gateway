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

function nowText() {
  return new Date().toLocaleTimeString("zh-CN", { hour12: false });
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

function updateFakeSensorData() {
  state.uptime += 2;
  state.temperature = Number((26.5 + Math.sin(state.uptime / 12) * 0.8).toFixed(1));
  state.humidity = Number((60.2 + Math.cos(state.uptime / 10) * 1.2).toFixed(1));
  state.light = Math.round(380 + Math.sin(state.uptime / 8) * 35);
  state.wifi = state.uptime >= 4 ? 1 : 0;
  state.mqtt = state.uptime >= 8 ? 1 : 0;
  state.device_state = state.mqtt ? "ONLINE" : state.wifi ? "MQTT_CONNECTING" : "WIFI_CONNECTING";
  snapshotHistory();
}

function drawTrendChart() {
  const canvas = document.getElementById("trend-chart");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  const padding = 34;
  const chartWidth = width - padding * 2;
  const chartHeight = height - padding * 2;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#f8fbfd";
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

  const series = [
    { key: "temperature", color: "#b45309", min: 20, max: 32 },
    { key: "humidity", color: "#2563eb", min: 45, max: 75 },
    { key: "light", color: "#0f766e", min: 300, max: 460 }
  ];

  series.forEach((line) => {
    ctx.strokeStyle = line.color;
    ctx.lineWidth = 3;
    ctx.beginPath();

    history.forEach((point, index) => {
      const x = padding + (chartWidth / Math.max(maxHistory - 1, 1)) * index;
      const ratio = (point[line.key] - line.min) / (line.max - line.min);
      const y = padding + chartHeight - Math.max(0, Math.min(1, ratio)) * chartHeight;
      if (index === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    });

    ctx.stroke();
  });

  ctx.fillStyle = "#657387";
  ctx.font = "14px Microsoft YaHei, Segoe UI, Arial";
  ctx.fillText("温度", padding, 22);
  ctx.fillText("湿度", padding + 52, 22);
  ctx.fillText("光照", padding + 104, 22);
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
  document.getElementById("temperature").textContent = state.temperature;
  document.getElementById("humidity").textContent = state.humidity;
  document.getElementById("light").textContent = state.light;
  document.getElementById("wifi").textContent = state.wifi ? "已连接" : "未连接";
  document.getElementById("mqtt").textContent = state.mqtt ? "已连接" : "未连接";
  document.getElementById("uptime").textContent = `${state.uptime} s`;
  document.getElementById("error-code").textContent = state.error_code;
  document.getElementById("firmware").textContent = state.firmware;

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
  button.addEventListener("click", () => {
    const target = button.dataset.target;
    state[target] = state[target] ? 0 : 1;
    pushLog(`${labels[target]}${state[target] ? "开启" : "关闭"}`, "控制状态已写入本地演示状态");
    render();
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
render();

setInterval(() => {
  updateFakeSensorData();
  if (state.uptime === 4) {
    pushLog("Wi-Fi 已连接", "Station 获取连接状态");
  }
  if (state.uptime === 8) {
    pushLog("MQTT 已连接", "设备状态切换为 ONLINE");
  }
  render();
}, 2000);
