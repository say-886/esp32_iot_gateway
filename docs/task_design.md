# FreeRTOS Task Design

| 任务 | 职责 | 周期/触发 |
| --- | --- | --- |
| `sensor_task` | 采集 AHT20、BH1750 数据 | 2s |
| `display_task` | 刷新 OLED 显示 | 1s |
| `button_task` | 按键扫描和消抖 | 10~20ms |
| `control_task` | 处理执行器命令 | 队列触发 |
| `web_task` | 处理 HTTP/Web 请求 | 事件驱动 |
| `mqtt_task` | MQTT 连接、上报、订阅 | 周期/事件驱动 |
| `monitor_task` | 心跳、看门狗、错误监控 | 1s |
