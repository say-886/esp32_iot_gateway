# Interview Notes

项目采用 ESP-IDF component 方式拆分模块，`main` 只负责启动流程和任务创建，具体能力放到 `components` 中。

可讲重点：

- `board` 统一维护 GPIO，避免引脚散落在业务代码中。
- `app_common` 保存设备状态、错误码和日志宏。
- `app_state` 使用状态机管理 INIT、联网、在线、异常和恢复。
- `device_control`、`sensor_*`、`wifi_manager`、`web_server`、`mqtt_service`、`storage_nvs` 分别封装功能边界。
- 后续可自然扩展 watchdog、OTA、RS485/Modbus 和 CAN。
