# ESP32 IoT Gateway 硬件接线表

## 当前引脚约定

| 功能 | ESP32 引脚 | 工程宏 | 说明 |
| --- | --- | --- | --- |
| I2C SDA | GPIO21 / D21 | `BOARD_I2C_SDA_GPIO` | AHT20、BH1750、后续 I2C OLED 共用 |
| I2C SCL | GPIO22 / D22 | `BOARD_I2C_SCL_GPIO` | AHT20、BH1750、后续 I2C OLED 共用 |
| LED | GPIO2 / D2 | `BOARD_LED_GPIO` / `BOARD_LED_1_GPIO` | 当前最小规格只接 1 个 LED，高电平点亮 |
| 蜂鸣器 | GPIO25 / D25 | `BOARD_BUZZER_GPIO` | GPIO 输出，高电平触发 |
| 单路继电器 | GPIO26 / D26 | `BOARD_RELAY_GPIO` | GPIO 输出，触发电平以模块为准 |
| 按键 K1 | GPIO27 / D27 | `BOARD_BUTTON_1_GPIO` | GPIO 输入，内部上拉，按下接 GND |
| 按键 K2 | GPIO14 / D14 | `BOARD_BUTTON_2_GPIO` | GPIO 输入，内部上拉，按下接 GND |
| 按键 K3 | GPIO32 / D32 | `BOARD_BUTTON_3_GPIO` | GPIO 输入，内部上拉，按下接 GND |
| 按键 K4 | GPIO33 / D33 | `BOARD_BUTTON_4_GPIO` | GPIO 输入，内部上拉，按下接 GND |

开发板自带 BOOT 按钮占用 GPIO0，保留给下载模式使用。项目外接按键不使用 GPIO0，避免影响 ESP32 启动。

## 电源轨

| ESP32 | 面包板 |
| --- | --- |
| 3V3 | 红色 `+` 电源轨 |
| GND | 蓝色 `-` 地线轨 |

注意事项：

- 当前传感器与小模块统一使用 3.3V。
- 面包板左右两侧电源轨通常不互通，同一侧中间也可能断开；使用前需要确认或用短线桥接。
- 所有模块必须共地，即模块 GND 与 ESP32 GND 接到同一条地线网络。

## AHT20 温湿度传感器

| AHT20 | ESP32 / 面包板 |
| --- | --- |
| VCC | 3V3 / 红色 `+` |
| GND | GND / 蓝色 `-` |
| SDA | GPIO21 / D21 |
| SCL | GPIO22 / D22 |

已验证地址：

```text
0x38
```

## BH1750 光照传感器

| BH1750 | ESP32 / 面包板 |
| --- | --- |
| VIN | 3V3 / 红色 `+` |
| GND | GND / 蓝色 `-` |
| SDA | GPIO21 / D21 |
| SCL | GPIO22 / D22 |

已验证地址：

```text
0x23
```

## 四位独立按键模块

你的按键模块丝印为：

```text
GND K4 K3 K2 K1
```

该模块不需要接 VCC。每个按键按下后，会把对应的 `Kx` 拉到 GND。工程中已启用 ESP32 内部上拉。

| 按键模块 | ESP32 / 面包板 |
| --- | --- |
| GND | GND / 蓝色 `-` |
| K1 | GPIO27 / D27 |
| K2 | GPIO14 / D14 |
| K3 | GPIO32 / D32 |
| K4 | GPIO33 / D33 |

注意事项：

- 接线前先断电。
- 按键模块的 `GND` 只需要接到任意一个公共 GND。
- 不要把 `K1~K4` 接到 3V3 或 5V。
- 不使用 GPIO34~GPIO39 做这些按键，因为它们没有内部上拉。

## LED

当前最小规格只接 1 个 LED，用于验证 GPIO 输出和后续状态指示。

### LED 模块

| LED 模块 | ESP32 / 面包板 |
| --- | --- |
| VCC / + | 3V3 / 红色 `+` |
| GND / - | GND / 蓝色 `-` |
| IN / S | 对应 LED 控制 GPIO |
| IN / S | GPIO2 / D2 |

### 裸 LED

裸 LED 必须串联限流电阻：

```text
GPIOx -> 220~330 ohm 电阻 -> LED 长脚
LED 短脚 -> GND
```

```text
GPIO2 / D2 -> 220~330 ohm 电阻 -> LED 长脚
LED 短脚 -> GND
```

注意事项：

- 不要把 LED 直接跨接在 GPIO 和 GND 之间。
- 如果使用 LED 模块，通常模块已经带限流电阻；如果不确定，仍建议串一个 220~330 ohm 电阻。
- 其余 LED 暂不接入。后续如果需要多状态指示，再扩展 LED2~LED4 和对应控制接口。

## 蜂鸣器模块

当前按三针有源蜂鸣器模块规划，常见接口为：

```text
S VCC GND
```

推荐接线：

| 蜂鸣器模块 | ESP32 / 面包板 | 说明 |
| --- | --- | --- |
| S | GPIO25 / D25 | 控制信号 |
| VCC | 3V3 / 红色 `+` | 优先使用 3.3V |
| GND | GND / 蓝色 `-` | 必须与 ESP32 共地 |

注意事项：

- 优先确认模块是有源蜂鸣器还是无源蜂鸣器。有源蜂鸣器给电平即可响，无源蜂鸣器需要 PWM。
- 当前代码 `device_buzzer_set(true)` 会让 GPIO25 输出高电平，适合高电平触发的有源蜂鸣器。
- 如果模块是低电平触发，接线不变，后续需要在 `device_buzzer_set()` 中反相。
- 如果模块要求 5V 供电，`VCC` 接外部 5V，但 `GND` 必须和 ESP32 GND 共地；信号脚仍接 GPIO25。
- 初次测试时不要长时间持续鸣叫，先短时间触发确认即可。

## 单路继电器模块

你的单路继电器控制端为：

```text
S VCC GND
```

推荐先这样接：

| 继电器模块 | ESP32 / 面包板 | 说明 |
| --- | --- | --- |
| S | GPIO26 / D26 | 控制信号 |
| VCC | 3V3 / 红色 `+` | 若模块标注必须 5V，则改用外部 5V |
| GND | GND / 蓝色 `-` | 必须与 ESP32 共地 |

继电器触点侧一般有：

```text
COM
NO
NC
```

当前阶段只测试继电器吸合/释放，不接强电负载。不要把 220V 市电接到面包板上。

注意事项：

- 有些继电器模块是低电平触发，`S=0` 吸合；有些是高电平触发，`S=1` 吸合。
- 当前 `device_relay_set(true)` 会让 GPIO26 输出高电平。
- 如果接上后发现逻辑反了，后续需要在 `device_relay_set()` 中反相。
- 如果继电器模块要求 5V 供电，`VCC` 接外部 5V，但 `GND` 必须和 ESP32 GND 共地。

## OLED（SSD1306 I2C）

当前工程已接入 `SSD1306 128x64` I2C OLED 驱动，并会在 `display_task` 中显示：

- 温度、湿度、光照
- WiFi / MQTT 状态
- LED / 蜂鸣器 / 继电器状态
- 运行时间、错误码、设备状态

推荐接线为：

| I2C OLED | ESP32 / 面包板 |
| --- | --- |
| VCC | 3V3 / 红色 `+` |
| GND | GND / 蓝色 `-` |
| SDA | GPIO21 / D21 |
| SCL | GPIO22 / D22 |

预期 I2C 地址通常为：

```text
0x3C 或 0x3D
```

注意：

- OLED 与 AHT20、BH1750 共用 `GPIO21/22` 的 I2C 总线。
- 若 `oled_init()` 失败，请优先检查 `VCC/GND/SDA/SCL` 和模块地址。
- 当前工程仍使用 `AHT20`，并未切换到 `DHT11`。
