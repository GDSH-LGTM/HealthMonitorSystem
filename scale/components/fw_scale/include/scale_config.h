#ifndef SCALE_CONFIG_H
#define SCALE_CONFIG_H

/* ============================================================
 * 体重秤工程配置
 *
 * ⚠️ 术语提醒（避免和手环工程搞混）：
 *   本文件叫 scale_config.h；下面的 SCALE_xxx 宏都指"秤重/标定"，
 *   与"缩放/比例"无关。手环工程用的是 app_config.h。
 * ============================================================ */

#define SCALE_FW_NAME     "health_monitor_scale"
#define SCALE_FW_VERSION  "1.2.0"

/* ============================================================
 * 一、HX711 称重 ADC
 * ============================================================ */
/* 接线（与手环的引脚分配完全不冲突，秤是独立板子）：
 *   HX711 DOUT → GPIO1    HX711 SCK → GPIO2
 *   HX711 VCC  → 3V3      HX711 GND → GND
 *   传感器（桥式压力片）4 线接 HX711 的 E+/E-/A+/A-
 * 建议 HX711 用 5V 供电时称重更稳（其 AVDD 与数字供电独立），
 * 但 DOUT/SCK 电平需确认与 3.3V MCU 兼容（多数模块板载电平适配）。 */
#define SCALE_HX711_PIN_DOUT  1
#define SCALE_HX711_PIN_SCK   2

/* 上电后 DOUT 保持高的时间（Datasheet 约 400ms），等够再读 */
#define HX711_POWER_UP_MS     450
/* 等 DOUT 拉低的最长时间：HX711 默认 10Hz，约 100ms 一个样本，留 3 倍余量 */
#define HX711_READY_TIMEOUT_MS 300
/* 中位数滤波取多少次（栈上静态数组，别太大） */
#define HX711_MEDIAN_MAX      15

/* ============================================================
 * 二、采样与稳定判定
 * ============================================================ */
#define SCALE_SAMPLE_PERIOD_MS   80    /* 采样周期；HX711 @10Hz 足够，读数由 ready 门控 */
#define SCALE_STABLE_WINDOW      6     /* 稳定判定窗口：最近 N 个样本 */
#define SCALE_STABLE_TOLERANCE_G 15.0f /* 窗口内极差 <= 该值（克）视为稳定 */
#define SCALE_STABLE_REQUIRED    3     /* 连续 N 次稳定判定才算稳定（防抖） */
#define SCALE_MIN_GRAMS          200.0f /* 小于该重量视为"空载"，不发送 */
#define SCALE_MAX_GRAMS          200000.0f /* 超过该值视为异常读数，丢弃 */

/* ============================================================
 * 三、传输（0x02 体重帧）
 *
 * 协议 §3：0x02 走"通道 A"。手环的通道 A 是 USB 串口，但秤没有多余的
 * USB 口，所以这里默认走 WiFi TCP。
 *
 * ⚠️ 需要在上位机侧确认接收方式：
 *   - 上位机的 scalePortName 是**串口**（COMx）。若秤走 TCP，
 *     则需要在 CM 里再加一个 TCP 监听（见 ../lower/docs/SCALE_TRANSPORT.md）。
 *   - 端口用 45679（与手环图像通道 45678 分开，避免抢单客户端）。
 * ============================================================ */
#define SCALE_TRANSPORT_TCP   1
#define SCALE_TRANSPORT_UART  2

#define SCALE_TRANSPORT       SCALE_TRANSPORT_TCP

#define SCALE_TCP_PORT        45679

/* 设备侧预置（协议规定 SSID/密码不经协议传输）。运行时可由 NVS 覆盖。 */
#define SCALE_DEFAULT_SSID      "152******75的nova 11"
#define SCALE_DEFAULT_PASSWORD  "12345679"

/* PC 的 IPv4 兜底值（协议要求由 0x09 下发，但存在启动死锁，故给默认值）。
 * 当前实测：Android 热点网段。 */
#define SCALE_DEFAULT_PC_IP     "192.168.43.190"

/* 断连退避：1s → 2s → 5s → 10s（协议 §10） */
#define SCALE_BACKOFF_MS_1     1000
#define SCALE_BACKOFF_MS_2     2000
#define SCALE_BACKOFF_MS_3     5000
#define SCALE_BACKOFF_MS_MAX   10000

/* ============================================================
 * 四、标定
 * ============================================================ */
/* 标定值存 NVS，掉电不丢。首次上电未标定时 scale=0，重量读数为 0，
 * 需要按 docs/CALIBRATION.md 走一遍去皮 + 砝码标定。 */
#define SCALE_NVS_NAMESPACE   "scale"
#define SCALE_NVS_KEY_OFFSET  "offset"
#define SCALE_NVS_KEY_SCALE   "scale"

#endif /* SCALE_CONFIG_H */
