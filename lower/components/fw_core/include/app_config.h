#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ============================================================
 * 全局配置：引脚 / 网络 / 通道角色
 *
 * 引脚依据 projects 交接文档「三、完整引脚分配表（最终版，已更新到代码）」，
 * 与旧 pin_config.h 一致（那份 datum\接线说明.md 是更早的错误版本，
 * 其中 LCD 用 GPIO39-42 会因 JTAG 引脚导致白屏，切勿采信）。
 *
 * 网络凭据一律不硬编码在代码里：优先从 NVS 读，缺省值只用于首次开机兜底。
 * ============================================================ */

/* ---------- 固件标识 ---------- */
#define APP_FW_NAME       "health_monitor_band"
#define APP_FW_VERSION    "1.2.0"

/* ============================================================
 * 一、LCD ILI9341（SPI3_HOST）
 * ============================================================ */
#define LCD_PIN_SCLK     1
#define LCD_PIN_MOSI     2
#define LCD_PIN_MISO     (-1)
#define LCD_PIN_CS       48
#define LCD_PIN_DC       3
#define LCD_PIN_RST      21
/* spi_host_device_t: SPI1_HOST=0, SPI2_HOST=1, SPI3_HOST=2（见 hal/spi_types.h）*/
#define LCD_SPI_HOST     2            /* SPI3_HOST */
#define LCD_SPI_FREQ_HZ  (10 * 1000 * 1000)
#define LCD_W            240
#define LCD_H            320

/* ============================================================
 * 二、触摸 XPT2046（与 LCD 共用 SCK/MOSI，独立 CS/IRQ/MISO）
 * GPIO39 是 JTAG(MTCK)，高速 SPI 不可用，但触摸这种低速 SPI 可以
 * ============================================================ */
#define TOUCH_PIN_CS     45
#define TOUCH_PIN_IRQ    46
#define TOUCH_PIN_MISO   39
#define TOUCH_SPI_FREQ_HZ (1 * 1000 * 1000)
/* XPT2046 原始 ADC 量程（12 位）。默认取常见模块的近似范围，
 * 首次校准用串口打印的原始值微调 —— 见 docs/LCD_TOUCH.md */
#define TOUCH_RAW_X_MIN  200
#define TOUCH_RAW_X_MAX  3900
#define TOUCH_RAW_Y_MIN  200
#define TOUCH_RAW_Y_MAX  3900
#define TOUCH_SWAP_XY    0
#define TOUCH_INVERT_X   0
#define TOUCH_INVERT_Y   0

/* ============================================================
 * 三、I2C（MAX30102 + SHT30 并联）
 * GPIO19/20 是 USB OTG D-/D+，用 Micro-USB 口烧录供电时可用
 * ============================================================ */
#define I2C_PORT_NUM     0
#define I2C_PIN_SCL      19
#define I2C_PIN_SDA      20
#define I2C_FREQ_HZ      100000
#define MAX30102_ADDR    0x57
#define SHT30_ADDR       0x44

/* ============================================================
 * 四、摄像头 ATK-OV2640（DVP + SCCB）
 * 模块板载 12MHz 有源晶振（原理图 Y1，OUT 直连 OV2640 XVCLK），
 * 排针未引出 XCLK → pin_xclk=-1，ESP32 不输出时钟。
 * 但 xclk_freq_hz 必须填晶振真实频率 12MHz（不能填 0）：
 * esp32_camera 驱动用 160000000/xclk_freq_hz 算分频，填 0 会除零 panic。
 * ============================================================ */
#define CAM_XCLK_HZ      12000000
#define CAM_PIN_D0       4
#define CAM_PIN_D1       5
#define CAM_PIN_D2       6
#define CAM_PIN_D3       7
#define CAM_PIN_D4       8
#define CAM_PIN_D5       9
#define CAM_PIN_D6       10
#define CAM_PIN_D7       11
#define CAM_PIN_VSYNC    12
#define CAM_PIN_HREF     13
#define CAM_PIN_PCLK     14
#define CAM_PIN_XCLK     (-1)
#define CAM_PIN_SCCB_SDA 17
#define CAM_PIN_SCCB_SCL 16
#define CAM_PIN_PWDN     18
#define CAM_PIN_RESET    47

/* JPEG 质量 0~63，越小质量越高、字节越多。10 ≈ QVGA 8~15KB（模拟源用的
 * test_food.jpg 是 12659 字节，13 个分片，量级一致） */
#define CAM_JPEG_QUALITY 10

/* ============================================================
 * 五、其他外设
 * ============================================================ */
#define MOTOR_PIN        15   /* 经 S8050 三极管驱动，高电平振动 */
#define LED_PIN          38   /* 板载 WS2812 RGB LED */
#define UART_NUM_DATA    0    /* 协议帧走 UART0（即 COM 口），兼作日志 */

/* ============================================================
 * 六、网络
 * ============================================================ */
/* 设备侧预置（协议规定 SSID/密码不经协议传输）。
 * 运行时可被 NVS 覆盖 —— 见 doc/NETWORK_CONFIG.md */
#define NET_DEFAULT_SSID      "瞳瞳的iphone"
#define NET_DEFAULT_PASSWORD  "1234567890"

/* PC 的 IPv4。协议要求由 PC 经 0x09 下发，但 0x09 依赖通道 A 已经通；
 * 而通道 A 又需要设备先连上网络 —— 存在启动死锁，故这里给一个兜底值。
 * ⚠️ 部署注意：把 PC 在热点里的 IPv4 固定下来（静态分配或 DHCP 保留），
 *    或在连接后由 0x09 动态刷新（fw_wifi_apply_pc_ip）。
 * 当前实测值：iPhone 热点网段 172.20.10.x，PC WLAN 地址 172.20.10.6。 */
#define NET_DEFAULT_PC_IP     "172.20.10.6"

/* 图像通道：PC = TCP 服务端（协议常量 45678），设备 = 客户端 */
#define NET_IMAGE_TCP_PORT   45678

/* 断连退避：1s → 2s → 5s → 10s（10s 封顶），见协议 §10 */
#define NET_BACKOFF_MS_1     1000
#define NET_BACKOFF_MS_2     2000
#define NET_BACKOFF_MS_3     5000
#define NET_BACKOFF_MS_MAX   10000

/* 设备身份（NVS 里没配时用默认值） */
#define NET_DEFAULT_DEVICE_NAME "health-band-01"

#endif /* APP_CONFIG_H */
