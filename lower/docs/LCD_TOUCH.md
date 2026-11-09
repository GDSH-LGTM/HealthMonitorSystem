# LCD（ILI9341）与触摸（XPT2046）说明

## 0. ⚠️ 先说一个硬件风险：三个 strapping 脚被外设占用

ESP32-S3 的 **strapping 脚是 GPIO0 / GPIO3 / GPIO45 / GPIO46**，ROM 在**硬件复位那一瞬间**
采样它们来决定启动模式：

| GPIO | 采样含义 | 低电平后果 |
|---|---|---|
| 0 | 启动模式 | 进入**下载模式**，不启动应用 |
| 3 | JTAG 源选择 | 可能选错 JTAG 源 |
| 45 | VDD_SPI 电压 | flash 供电电压选错 |
| 46 | ROM 启动相关 | 启动行为异常 |

本设计的引脚分配（继承自旧代码）把其中**三个**放在了外设上：

| 外设信号 | GPIO | 谁在复位期间可能拉低它 |
|---|---|---|
| `LCD_PIN_DC` | **3** | LCD 模块的 DC 网络（部分模块带下拉） |
| `TOUCH_PIN_CS` | **45** | 触摸片选网络 / 排线容性负载 |
| `TOUCH_PIN_IRQ` | **46** | 触摸控制器在未上电时把 IRQ 拉低 |

**现象**：上电不启动、或"时好时坏"（取决于上电顺序与触摸芯片状态）。

### 当前状态

**目前是好的** —— 串口 ROM 日志显示 `boot:0xc (SPI_FAST_FLASH_BOOT)`，
说明复位时 GPIO3 等确实被抬到了高电平（靠模块内部弱上拉或载板上的上拉）。

### 固件侧做了什么

`app_main` 一开头（在任何驱动之前）调用 `fw_lcd_harden_straps()`，
把 GPIO0/3/45/46 显式设为**输入 + 上拉**：

```c
fw_lcd_harden_straps();   /* main.c，在 uart_data_init() 之前 */
```

**但要清楚它的边界**：采样发生在硬件复位时刻，软件在其之后才运行，
所以它**修不了本次启动**。它能做的是把这三个脚的初始状态定成确定值，
从而消除**后续复位**时被拉低的机会，并把这处风险写进代码。

**真正的保证在硬件**：如果实测出现上电不启动，正确的修法是
①在 GPIO3/45/46 上加 10k 上拉到 3V3，或 ②把这几个信号换到非 strapping 脚
（可选的非 strapping 脚很有限，见交接文档的引脚占用表）。

---

## 1. 引脚

依据交接文档「三、完整引脚分配表（最终版，已更新到代码）」。

### LCD ILI9341 —— SPI3_HOST

| 信号 | GPIO | 说明 |
|---|---|---|
| SCLK | **1** | SPI 时钟 |
| MOSI | **2** | 主机输出 |
| MISO | **-1** | LCD 不用 MISO |
| CS | **48** | 片选 |
| DC | **3** | 数据/命令 |
| RST | **21** | 硬件复位 |
| 频率 | 10 MHz | 杜邦线可用；更稳可降到 1~5 MHz |

### 触摸 XPT2046 —— 与 LCD 共用 SCK/MOSI

| 信号 | GPIO | 说明 |
|---|---|---|
| T_CLK | 1 | 共用 LCD SCK，不单独接 |
| T_DIN | 2 | 共用 LCD MOSI，不单独接 |
| T_DO(MISO) | **39** | 触摸 MISO。GPIO39 是 JTAG(MTCK)，**低速 SPI 可用** |
| T_CS | **45** | 触摸片选 |
| T_IRQ | **46** | 触摸中断（低有效） |
| 频率 | 1 MHz | 触摸不宜高速 |

> ⚠️ **不要用 `datum\接线说明.md` 里那份接线表**——它把 LCD 放在 GPIO40/41/42，
> 那三个是 JTAG 引脚，高速 SPI 会导致**白屏**。这正是交接文档里"LCD 白屏"的根源。

---

## 2. 为什么触摸 MISO 是 GPIO39 而 LCD MISO 是 -1

ESP32-S3 的 GPIO39~42 默认是 JTAG（MTCK/MTDO/MTDI/MTMS）：

- **高速 SPI**（LCD，10MHz）走这些脚会白屏/花屏 → LCD 不用 MISO，配置成 `-1`
- **低速 SPI**（触摸，1MHz）可以走 → 触摸 MISO 用 GPIO39

两者共用同一个 SPI 主机，所以主机的 MISO 填 39（LCD 不读数据，不受影响）。

另外 `sdkconfig.defaults` 里关了 USB Serial/JTAG：

```
CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
```

这样 GPIO39~42 彻底释放给 SPI 用，并且强制走 Micro-USB(UART0) 口烧录与看日志。

---

## 3. 触摸标定

XPT2046 输出 12 位原始 ADC 值，需要映射到 240×320 屏幕坐标。
当前默认值在 `app_config.h`：

```c
#define TOUCH_RAW_X_MIN  200
#define TOUCH_RAW_X_MAX  3900
#define TOUCH_RAW_Y_MIN  200
#define TOUCH_RAW_Y_MAX  3900
#define TOUCH_SWAP_XY    0
#define TOUCH_INVERT_X   0
#define TOUCH_INVERT_Y   0
```

### 怎么判断要不要调

烧录后手指点屏幕**左上角**，看光标落在哪：

| 现象 | 要改的项 |
|---|---|
| 光标位置整体偏移/到不了边 | `RAW_*_MIN/MAX` 收紧或放宽 |
| 左右反了 | `TOUCH_INVERT_X 1` |
| 上下反了 | `TOUCH_INVERT_Y 1` |
| 横竖颠倒（点左边光标上下动） | `TOUCH_SWAP_XY 1` |

不用重新编译也能调：代码已提供运行时接口

```c
void fw_touch_set_calibration(uint16_t x_min, uint16_t x_max,
                              uint16_t y_min, uint16_t y_max);
void fw_touch_set_orientation(bool swap_xy, bool invert_x, bool invert_y);
```

以及读原始值用 `fw_touch_read_raw(&rx, &ry)`——在四个角各点一次记录原始值，
填入上表即可得到精确标定。

---

## 4. LVGL 配置方式

LVGL **8.4.0 本地内置**在 `components/lvgl/`（从旧工程复制，无需联网下载）。

**关键**：ESP-IDF 不递归扫描嵌套组件，所以 lvgl 必须在 `components/lvgl`
这样的**直接子目录**下。放进 `components/fw_lcd/lvgl` 会报：

```
Failed to resolve component 'lvgl' required by component 'fw_lcd': unknown name.
```

配置走 Kconfig（`CONFIG_LV_CONF_SKIP=y`，不需要 `lv_conf.h`），在
`sdkconfig.defaults` 里设置。本项目只开了 label/line/theme，
关掉了 calendar/keyboard/tabview/chart 等大件以省 flash 和 RAM。

### 绘制缓冲

`fw_ui.c` 用**两块 240×20 像素**缓冲（每块 9.6KB，共 19.2KB，放内部 DMA 内存）：

```c
#define UI_DRAW_BUF_LINES 20
```

**故意不用整屏缓冲**（240×320×2 = 150KB）——那必须依赖 PSRAM，
而本板是否有 PSRAM 还没确认（见 `docs/BUILD_FLASH.md` 第六节）。
这样即使板子没有 PSRAM，界面也能正常工作。

### LVGL 节拍

用 `esp_timer` 每 1ms 调 `lv_tick_inc(1)`，`fw_ui_loop()` 自己按 10ms 节拍调
`lv_timer_handler()`。不依赖外部传节拍，也不需要开 `CONFIG_LV_TICK_CUSTOM`。

---

## 5. 界面内容

`fw_ui.c` 建的界面：

- 标题 `Health Monitor`（青色）
- `HR`（红）/ `SpO2`（蓝）/ `Temp`（黄）/ `Hum`（绿）四行，无效值显示 `--`
- 心率降级时值后面加 `?`（对应协议 `hr_state == 2`）
- 底部两行通道状态：
  - `WiFi: OK` / `WiFi: connecting...`
  - `Image: OK <PC IP>` / `Image: off <PC IP>`

数据由 main 的 UI 任务每 500ms 推一次（`fw_ui_update_vitals` /
`fw_ui_update_status`）。**fw_lcd 不依赖 fw_sensors**，避免组件循环依赖。
