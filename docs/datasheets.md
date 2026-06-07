# Datasheets Index

本项目默认只提交数据手册索引，不提交大体积 PDF。需要离线保存时，可把 PDF 放入本地 `datasheets/` 目录；该目录已被 `.gitignore` 忽略。

| 器件/模块 | 用途 | 资料链接 | 重点关注 |
| --- | --- | --- | --- |
| ESP32-WROOM-32 | 主控模块 | [Espressif ESP32-WROOM-32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf) | 引脚定义、启动模式、电气特性、外围参考电路 |
| ESP32 硬件设计指南 | ESP32 硬件设计参考 | [Espressif ESP32 Hardware Design Guidelines](https://www.espressif.com/sites/default/files/documentation/esp32_hardware_design_guidelines_en.pdf) | 电源、EN/BOOT、晶振、RF、下载电路 |
| AHT20 | 温湿度传感器 | [ASAIR AHT20 Datasheet](https://datasheet4u.com/datasheet-pdf/ASAIR/AHT20/pdf.php?id=1551700) | I2C 地址、初始化流程、测量命令、数据换算 |
| BH1750 | 光照传感器 | [ROHM BH1750FVI Datasheet](https://fscdn.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1750fvi-e.pdf) | I2C 地址、测量模式、lux 换算 |
| SSD1306 | OLED 控制器 | [Solomon Systech SSD1306 Datasheet via DigiKey](https://www.digikey.com/htmldatasheets/production/2097726/0/0/1/ssd1306.html) | I2C/SPI 接口、初始化命令、页地址模式 |
| 继电器模块 | 执行器输出 | 以实际购买模块资料为准 | 触发电平、线圈电流、隔离方式、负载能力 |
| 有源蜂鸣器模块 | 告警输出 | 以实际购买模块资料为准 | 供电电压、触发电平、电流 |

硬件到货后，需要把实际模块型号、购买链接、触发电平和最终 GPIO 更新到 `docs/hardware_connection.md`。
