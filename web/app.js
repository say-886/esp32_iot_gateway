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
  led: "LED",
  buzzer: "蜂鸣器",
  relay: "继电器"
};

function updateFakeSensorData() {
  state.uptime += 2;
  state.temperature = Number((26.5 + Math.sin(state.uptime / 12) * 0.8).toFixed(1));
  state.humidity = Number((60.2 + Math.cos(state.uptime / 10) * 1.2).toFixed(1));
  state.light = Math.round(380 + Math.sin(state.uptime / 8) * 35);
  state.wifi = state.uptime >= 4 ? 1 : 0;
  state.mqtt = state.uptime >= 8 ? 1 : 0;
  state.device_state = state.mqtt ? "ONLINE" : state.wifi ? "MQTT_CONNECTING" : "WIFI_CONNECTING";
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
  document.getElementById("device-state").textContent = state.device_state;
  document.getElementById("payload").textContent = JSON.stringify(state, null, 2);

  document.querySelectorAll(".control-button").forEach((button) => {
    const target = button.dataset.target;
    const enabled = Boolean(state[target]);
    button.classList.toggle("is-on", enabled);
    button.textContent = `${labels[target]}：${enabled ? "开启" : "关闭"}`;
  });
}

document.querySelectorAll(".control-button").forEach((button) => {
  button.addEventListener("click", () => {
    const target = button.dataset.target;
    state[target] = state[target] ? 0 : 1;
    render();
  });
});

render();
setInterval(() => {
  updateFakeSensorData();
  render();
}, 2000);
