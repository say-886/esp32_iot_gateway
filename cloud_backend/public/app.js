"use strict";

const state = {
  latest: null,
  history: [],
  platformStatus: null,
  controls: {
    led: 0,
    buzzer: 0,
    relay: 0
  }
};

function $(id) {
  return document.getElementById(id);
}

function formatTime(value) {
  if (!value) {
    return "--";
  }
  return new Date(value).toLocaleString("zh-CN", { hour12: false });
}

async function getJson(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

async function postJson(url, body) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const payload = await response.json();
  if (!response.ok || !payload.ok) {
    throw new Error(payload.error || `HTTP ${response.status}`);
  }
  return payload;
}

function drawChart() {
  const canvas = $("chart");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  const pad = 38;
  const chartWidth = width - pad * 2;
  const chartHeight = height - pad * 2;
  const rows = 4;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "#d9e2ec";
  ctx.lineWidth = 1;

  for (let i = 0; i <= rows; i += 1) {
    const y = pad + (chartHeight / rows) * i;
    ctx.beginPath();
    ctx.moveTo(pad, y);
    ctx.lineTo(width - pad, y);
    ctx.stroke();
  }

  const series = [
    { key: "temperature", color: "#b45309", min: 0, max: 50 },
    { key: "humidity", color: "#2563eb", min: 0, max: 100 },
    { key: "light", color: "#0f766e", min: 0, max: 1000 }
  ];
  const data = state.history.slice(-40);

  series.forEach((line) => {
    const points = data.map((point, index) => {
      const ratio = Math.max(0, Math.min(1, (Number(point[line.key]) - line.min) / (line.max - line.min)));
      return {
        x: pad + (chartWidth / Math.max(data.length - 1, 1)) * index,
        y: pad + chartHeight - ratio * chartHeight
      };
    });
    if (!points.length) {
      return;
    }
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
  });

  ctx.fillStyle = "#667085";
  ctx.font = "14px Microsoft YaHei, Segoe UI, Arial";
  ctx.fillText("温度", pad, 23);
  ctx.fillText("湿度", pad + 52, 23);
  ctx.fillText("光照", pad + 104, 23);
}

function render() {
  const latest = state.latest;
  const online = Boolean(latest);
  const mqttConnected = Boolean(state.platformStatus?.mqtt?.connected);
  $("connection-state").textContent = online
    ? `${latest.device_id || "未知设备"} / MQTT ${mqttConnected ? "在线" : "重连中"}`
    : `等待设备数据 / MQTT ${mqttConnected ? "在线" : "重连中"}`;
  $("connection-state").classList.toggle("online", online);
  $("temperature").textContent = latest ? latest.temperature.toFixed(1) : "--";
  $("humidity").textContent = latest ? latest.humidity.toFixed(1) : "--";
  $("light").textContent = latest ? Math.round(latest.light) : "--";
  $("last-update").textContent = latest ? `最近更新 ${formatTime(latest.received_at)}` : "--";
  $("payload").textContent = JSON.stringify(latest || {}, null, 2);
  drawChart();
}

async function refresh() {
  try {
    const [latest, history, platformStatus] = await Promise.all([
      getJson("/api/sensor/latest"),
      getJson("/api/sensor/history?limit=80"),
      getJson("/api/status")
    ]);
    state.latest = latest.data;
    state.history = history.data || [];
    state.platformStatus = platformStatus;
    render();
  } catch (error) {
    $("connection-state").textContent = `接口异常: ${error.message}`;
  }
}

document.querySelectorAll("button[data-target]").forEach((button) => {
  button.addEventListener("click", async () => {
    const target = button.dataset.target;
    const next = state.controls[target] ? 0 : 1;
    button.disabled = true;
    try {
      const result = await postJson("/api/cmd", {
        device_id: state.latest?.device_id,
        [target]: next
      });
      state.controls[target] = next;
      $("command-log").textContent = JSON.stringify(result, null, 2);
    } catch (error) {
      $("command-log").textContent = JSON.stringify({ ok: false, error: error.message }, null, 2);
    } finally {
      button.disabled = false;
    }
  });
});

refresh();
setInterval(refresh, 2000);
