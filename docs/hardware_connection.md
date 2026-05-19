# Hardware Connection

硬件未到货，以下为接线表模板。最终 GPIO 以实际开发板丝印和测试结果为准。

| 模块 | 信号 | ESP32 GPIO | 说明 |
| --- | --- | --- | --- |
| AHT20 | SDA | GPIO21 | I2C 数据线 |
| AHT20 | SCL | GPIO22 | I2C 时钟线 |
| BH1750 | SDA | GPIO21 | 与 AHT20 共用 I2C |
| BH1750 | SCL | GPIO22 | 与 AHT20 共用 I2C |
| OLED SSD1306 | SDA | GPIO21 | 与传感器共用 I2C |
| OLED SSD1306 | SCL | GPIO22 | 与传感器共用 I2C |
| LED | IN | GPIO2 | GPIO 输出 |
| 蜂鸣器 | IN | GPIO25 | GPIO 输出 |
| 继电器 | IN | GPIO26 | GPIO 输出 |
| 按键 1 | OUT | GPIO0 | GPIO 输入 |
