/* ESP32-S3 健康监测手环 —— 主程序
 *
 * 双通道（协议 v1.2）：
 *   通道 A：体征 / 控制 —— UART0 → PC 的 COM 口（协议帧与日志共用，PC 侧当串口处理）
 *   通道 B：图像        —— WiFi TCP，PC 服务端 45678，设备客户端
 *
 * 数据链路：
 *   传感器采样 → fw_band_app（体征组帧 / 环形日志 / 0x03 校时 / 0x05 补传）
 *              → 通道 A 发送
 *   PC 0x0B 拍照 → 摄像头 JPEG → 0x06/0x07/0x08 分片 → 通道 B 发送
 *   PC 0x09 → 刷新 PC IPv4 → 图像通道按新地址重连
 *
 * 分层：main 只做装配与任务编排；协议在 fw_core，网络在 fw_net，
 *       摄像头在 fw_camera，传感器在 fw_sensors。 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "fw_proto.h"
#include "fw_hal.h"
#include "fw_band_app.h"
#include "fw_host.h"
#include "fw_image_tx.h"
#include "fw_wifi.h"
#include "band_sensors.h"
#include "fw_lcd.h"
#include "fw_touch.h"
#include "fw_ui.h"
#include "fw_camera.h"

#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_chip_info.h"

static const char *TAG = "main";

/* ==================== 通道 A：UART0 ==================== */
/* UART0 默认只装了 console，直接 uart_write_bytes 会报 "uart driver error"。
 * 这里显式安装驱动（收发缓冲），日志与协议帧可共存。 */
#define UART_RX_BUF   1024
#define UART_TX_BUF   4096

static void uart_data_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_DATA, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_DATA, &cfg));
    /* 引脚用 UART0 默认（GPIO43 TX / GPIO44 RX），不动 */
}

/* ==================== 通道 A 发送适配 ==================== */
static void uart_send_adapter(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (data == NULL || len == 0) return;
    uart_write_bytes(UART_NUM_DATA, (const char *)data, len);
}

/* ==================== 软时钟 ==================== */
/* epoch 基准：0x03 校时写入；未校时期间 epoch_now 返回 0（协议 §6：时间不可信，不补传） */
static uint32_t s_epoch_base;      /* 校时时刻的 epoch */
static uint32_t s_epoch_base_ms;   /* 校时时刻的 monotonic ms */
static int      s_epoch_synced;

static uint32_t mono_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint32_t clk_epoch_now(void *ctx)
{
    (void)ctx;
    if (!s_epoch_synced) return 0;
    return s_epoch_base + (mono_ms() - s_epoch_base_ms) / 1000u;
}

static void clk_set_epoch(void *ctx, uint32_t epoch)
{
    (void)ctx;
    s_epoch_base    = epoch;
    s_epoch_base_ms = mono_ms();
    s_epoch_synced  = 1;
    ESP_LOGI(TAG, "校时：epoch = %u", (unsigned)epoch);
}

static uint32_t clk_millis(void *ctx)
{
    (void)ctx;
    return mono_ms();
}

/* ==================== 离线缓存：板载 Flash ==================== */
/* 说明：为先把链路跑通，本版用 PSRAM/RAM 环形缓冲（sector_size = 0 = RAM 模式）。
 * fw_log 的 API 与 SPI Flash 版完全一致，后续换成真实 Flash 只需替换这三个回调，
 * 核心代码零改动。 */

#define LOG_SLOT_BYTES   (4 + FW_VITAL_FRAME)      /* 序号 4 + 整帧 24 = 28 */
#define LOG_SLOT_COUNT   4096                      /* ≈ 4096 帧 ≈ 68 分钟 */
#define LOG_RAM_SIZE     (LOG_SLOT_BYTES * LOG_SLOT_COUNT)   /* 112KB，放 PSRAM */
#define LOG_FALLBACK_SLOTS 256                     /* PSRAM 不可用时内部 RAM 兜底 */
static uint8_t *s_log_ram;
static uint32_t s_log_size;

static int storage_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (s_log_ram == NULL || addr > s_log_size || len > s_log_size - addr) return -1;
    memcpy(buf, s_log_ram + addr, len);
    return 0;
}

static int storage_program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (s_log_ram == NULL || addr > s_log_size || len > s_log_size - addr) return -1;
    memcpy(s_log_ram + addr, buf, len);
    return 0;
}

static int storage_erase_sector(void *ctx, uint32_t sector_index)
{
    (void)ctx; (void)sector_index;
    return 0;   /* RAM 模式不擦除 */
}

/* ==================== 手环应用实例 ==================== */
static fw_band_app_t     s_app;
static fw_storage_t      s_storage;
static fw_uart_t         s_uart;
static fw_clock_t        s_clock;

/* 最近一次采样值（供 LVGL 显示 / 日志用） */
static volatile fw_vital_t s_last_vital;

/* 传感器采样回调 —— 由 fw_sensors 提供实现 */
extern void band_sensors_read(fw_vital_t *out);

static void sample_callback(void *ctx, fw_vital_t *out)
{
    (void)ctx;
    band_sensors_read(out);
    out->ts = clk_epoch_now(NULL);       /* ts 由 app 覆盖，这里给出一致值 */
    s_last_vital = *out;
}

/* ==================== 任务 ==================== */

/* ==================== 下行帧路由与摄像头 ==================== */
/* 0x03/0x05 转交 fw_band_app；0x09/0x0B/0x0A 是板级动作，由这里处理 */
static fw_host_router_t s_host;

/* 0x0B 拍照请求：由 uart_rx 置位，摄像头任务取走（单槽，后到的覆盖前者） */
static volatile int      s_cap_req;
static volatile uint8_t  s_cap_image_id;
static volatile uint32_t s_cap_ok;
static volatile uint32_t s_cap_fail;
/* 拍照失败的告警限流：上位机若连续点"拍照识别"，同一原因会重复打印。
 * 只在首次与每 100 次打一条，成功一次就清零（问题消失后计数重新开始）。 */
static uint32_t s_warn_no_channel;
static uint32_t s_warn_no_camera;

/* 图像发送回调：把组好的帧写到通道 B */
static int image_send_cb(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    return fw_wifi_image_send(data, len);
}

static void on_capture_cmd(void *ctx, uint8_t image_id)
{
    (void)ctx;
    if (image_id < FW_IMG_ID_MIN || image_id > FW_IMG_ID_MAX) {
        ESP_LOGW(TAG, "0x0B 图 ID 越界：0x%02X（应在 0x01~0x7F），丢弃", image_id);
        return;
    }
    s_cap_image_id = image_id;
    s_cap_req = 1;
    ESP_LOGI(TAG, "收到 0x0B 拍照指令，图 ID = %u", (unsigned)image_id);
}

static void on_wifi_cfg_cmd(void *ctx, const uint8_t ip[4])
{
    (void)ctx;
    fw_wifi_apply_pc_ip(ip);      /* 内部幂等 + 地址变化时立即重连 */
}

static void on_motor_cmd(void *ctx, uint8_t warn_type, uint8_t motor_mode)
{
    (void)ctx;
    /* 0x0A 协议已分配但本版不实现（协议 §4.10）；下位机侧只记日志，
     * 真正的预警震动以本地判定（sample_callback 里）为准 */
    ESP_LOGI(TAG, "收到 0x0A 震动指令（本版不实现）：warn=%u mode=%u",
             warn_type, motor_mode);
}

/* 拍照任务：收到 0x0B 后采集 → 组 0x06/0x07/0x08 → 走通道 B 发出 */
static void camera_task(void *arg)
{
    (void)arg;

    for (;;) {
        if (s_cap_req) {
            const uint8_t *jpeg = NULL;
            uint32_t       len  = 0;
            void          *frame = NULL;
            uint8_t        id = s_cap_image_id;
            uint32_t       ts;

            s_cap_req = 0;

            /* 通道未就绪 → 直接丢弃，不缓存图 ID（协议 §8.6） */
            if (!fw_wifi_image_connected()) {
                ++s_warn_no_channel;
                if (s_warn_no_channel == 1 || (s_warn_no_channel % 100) == 0) {
                    ESP_LOGW(TAG, "图像通道未就绪，丢弃拍照请求（协议 §8.6 不缓存，"
                                  "累计 %u 次）", (unsigned)s_warn_no_channel);
                }
                ++s_cap_fail;
                continue;
            }
            if (!fw_camera_is_ready()) {
                ++s_warn_no_camera;
                if (s_warn_no_camera == 1 || (s_warn_no_camera % 100) == 0) {
                    ESP_LOGW(TAG, "摄像头不可用：%s（累计 %u 次）",
                             fw_camera_last_error(), (unsigned)s_warn_no_camera);
                }
                ++s_cap_fail;
                continue;
            }

            if (fw_camera_capture(&jpeg, &len, &frame) != 0) {
                ESP_LOGW(TAG, "拍照失败：%s", fw_camera_last_error());
                ++s_cap_fail;
                continue;
            }

            /* 三帧共用同一采集时刻（协议 §2） */
            ts = clk_epoch_now(NULL);

            {
                fw_image_tx_result_t r =
                    fw_image_tx_send(jpeg, len, id, ts, image_send_cb, NULL);

                if (r == FW_IMAGE_TX_OK) {
                    ++s_cap_ok;
                    s_warn_no_channel = 0;   /* 成功了就把限流计数清零 */
                    s_warn_no_camera  = 0;
                    ESP_LOGI(TAG, "图 ID=%u 已发送：%u 字节 / %u 片",
                             (unsigned)id, (unsigned)len,
                             (unsigned)fw_image_fragment_count(len));
                } else {
                    ++s_cap_fail;
                    ESP_LOGW(TAG, "图像发送失败（result=%d，%u 字节）", (int)r, (unsigned)len);
                }
            }

            fw_camera_return_frame(frame);   /* 必须归还，否则缓冲池耗尽 */
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static int camera_init(void)
{
    const fw_camera_cfg_t cam = {
        .pin_d0 = CAM_PIN_D0, .pin_d1 = CAM_PIN_D1, .pin_d2 = CAM_PIN_D2, .pin_d3 = CAM_PIN_D3,
        .pin_d4 = CAM_PIN_D4, .pin_d5 = CAM_PIN_D5, .pin_d6 = CAM_PIN_D6, .pin_d7 = CAM_PIN_D7,
        .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,   .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SCCB_SDA, .pin_sccb_scl = CAM_PIN_SCCB_SCL,
        .pin_pwdn = CAM_PIN_PWDN,   .pin_reset = CAM_PIN_RESET,
        .frame_width = LCD_W, .frame_height = LCD_H,
        .jpeg_quality = CAM_JPEG_QUALITY,
    };
    return fw_camera_init(&cam);
}

/* 通道 A 接收：字节流 → 下行帧路由（0x09/0x0B/0x0A 就地处理，0x03/0x05 转交 app） */
static void uart_rx_task(void *arg)
{
    uint8_t buf[256];
    (void)arg;

    for (;;) {
        int n = uart_read_bytes(UART_NUM_DATA, buf, sizeof(buf),
                                pdMS_TO_TICKS(100));
        if (n > 0) {
            fw_host_feed(&s_host, buf, n, &s_app);
        }
    }
}

/* 应用节拍：采样 / 补传调度（建议 ≤1ms 周期，见 fw_band_app.h 调用约定） */
static void app_tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        fw_band_app_tick(&s_app);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ==================== 通道 B 状态播报 ==================== */
static void net_status_task(void *arg)
{
    (void)arg;
    for (;;) {
        int connected = 0;
        if (fw_wifi_take_state_change(&connected)) {
            char ip[16];
            fw_wifi_pc_ip_str(ip, sizeof(ip));
            ESP_LOGI(TAG, "图像通道状态：%s（PC %s:%d）",
                     connected ? "已连接" : "已断开", ip, NET_IMAGE_TCP_PORT);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ==================== LCD + 触摸 + 界面 ==================== */
static int lcd_touch_ui_init(void)
{
    const fw_lcd_pins_t lcd = {
        .spi_host    = LCD_SPI_HOST,
        .pin_sclk    = LCD_PIN_SCLK,
        .pin_mosi    = LCD_PIN_MOSI,
        .pin_miso    = LCD_PIN_MISO,   /* -1：LCD 不用 MISO */
        .pin_cs      = LCD_PIN_CS,
        .pin_dc      = LCD_PIN_DC,
        .pin_rst     = LCD_PIN_RST,
        .spi_freq_hz = LCD_SPI_FREQ_HZ,
        .width       = LCD_W,
        .height      = LCD_H,
    };
    if (fw_lcd_init(&lcd) != 0) {
        ESP_LOGE(TAG, "LCD 初始化失败 —— 检查接线（SCLK=1 MOSI=2 CS=48 DC=3 RST=21）");
        return -1;
    }

    const fw_touch_cfg_t tch = {
        .spi_host    = LCD_SPI_HOST,   /* 与 LCD 共用 SCK/MOSI */
        .pin_cs      = TOUCH_PIN_CS,
        .pin_irq     = TOUCH_PIN_IRQ,
        .spi_freq_hz = TOUCH_SPI_FREQ_HZ,
        .screen_w    = LCD_W,
        .screen_h    = LCD_H,
        .raw_x_min   = TOUCH_RAW_X_MIN,
        .raw_x_max   = TOUCH_RAW_X_MAX,
        .raw_y_min   = TOUCH_RAW_Y_MIN,
        .raw_y_max   = TOUCH_RAW_Y_MAX,
        .swap_xy     = TOUCH_SWAP_XY,
        .invert_x    = TOUCH_INVERT_X,
        .invert_y    = TOUCH_INVERT_Y,
    };
    if (fw_touch_init(&tch) != 0) {
        ESP_LOGW(TAG, "触摸初始化失败，界面仍可用（只是不能点）");
    }

    if (fw_ui_init() != 0) {
        ESP_LOGE(TAG, "LVGL 界面初始化失败");
        return -1;
    }
    return 0;
}

/* UI 任务：LVGL 节拍 + 每 500ms 把最新数据与通道状态推给界面 */
static void ui_task(void *arg)
{
    int  tick = 0;
    (void)arg;

    for (;;) {
        fw_ui_loop();

        if ((tick++ % 50) == 0) {          /* 50 x 10ms = 500ms */
            fw_vital_t v;
            char ip[16] = {0};
            float t, h;

            band_sensors_read(&v);
            fw_wifi_pc_ip_str(ip, sizeof(ip));

            t = (v.temp_x10 == FW_TEMP_INVALID) ? -1.0f : (v.temp_x10 / 10.0f);
            h = (v.hum_x10  == FW_HUM_INVALID)  ? -1.0f : (v.hum_x10  / 10.0f);

            fw_ui_update_vitals((v.hr == FW_HR_INVALID) ? -1 : (int)v.hr,
                                (v.spo2 == FW_BO_INVALID) ? -1 : (int)v.spo2,
                                t, h, (int)v.hr_state);
            fw_ui_update_status(fw_wifi_is_ready(),
                                fw_wifi_image_connected(), ip);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ==================== 启动自检报告 ==================== */
/* 板子模组丝印看不到型号，PSRAM 到底有没有由这里报出来（也决定摄像头帧缓冲放哪） */
static void log_boot_report(void)
{
    esp_chip_info_t chip;
    size_t psram = esp_psram_get_size();

    esp_chip_info(&chip);
    ESP_LOGI(TAG, "芯片: %s, %d 核, 硅片版本 v%d.%d, 主频 %d MHz",
             CONFIG_IDF_TARGET, chip.cores,
             chip.revision / 100, chip.revision % 100,
             (int)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    if (psram > 0) {
        ESP_LOGI(TAG, "PSRAM: %u 字节（%.1f MB）—— 可用，摄像头帧缓冲放 PSRAM",
                 (unsigned)psram, psram / 1048576.0);
    } else {
        ESP_LOGW(TAG, "PSRAM: 未检测到！本板可能是 N16 而不是 N16R8。"
                      "摄像头需改用 CAMERA_FB_IN_DRAM 且 fb_count=1");
    }
    ESP_LOGI(TAG, "空闲内存: 内部 %u 字节 / PSRAM %u 字节",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* ==================== main ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s 启动", APP_FW_NAME, APP_FW_VERSION);

    /* 板载 WS2812（LED_PIN=38）：上电即拉低数据线熄灭，避免保持旧点亮状态 */
    {
        const gpio_config_t led = {
            .pin_bit_mask = (1ULL << LED_PIN),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = 0,
            .pull_down_en = 0,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t e = gpio_config(&led);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "GPIO%u 配置失败: %s", (unsigned)LED_PIN, esp_err_to_name(e));
        } else {
            gpio_set_level(LED_PIN, 0);
        }
    }

    /* NVS（WiFi 依赖） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    log_boot_report();

    /* strapping 加固：LCD_DC=GPIO3 / TOUCH_CS=GPIO45 / TOUCH_IRQ=GPIO46 都是
     * ESP32-S3 的 strapping 脚，必须在任何会驱动它们的驱动之前把它们显式定为
     * 输入+上拉。详见 fw_lcd.h 的 fw_lcd_harden_straps() 说明。 */
    fw_lcd_harden_straps();

    /* 通道 A：先装 UART 驱动，再初始化应用（否则 uart_write_bytes 报错） */
    uart_data_init();

    /* 离线日志缓冲（112KB）放 PSRAM，腾出内部 RAM 给 WiFi DMA；失败用内部小缓冲兜底 */
    s_log_ram = heap_caps_malloc(LOG_RAM_SIZE, MALLOC_CAP_SPIRAM);
    if (s_log_ram) {
        s_log_size = LOG_RAM_SIZE;
    } else {
        s_log_ram = heap_caps_malloc(LOG_SLOT_BYTES * LOG_FALLBACK_SLOTS, MALLOC_CAP_INTERNAL);
        s_log_size = s_log_ram ? (LOG_SLOT_BYTES * LOG_FALLBACK_SLOTS) : 0;
        ESP_LOGW(TAG, "离线日志缓冲降级内部 RAM：%u 字节", (unsigned)s_log_size);
    }
    ESP_LOGI(TAG, "离线日志缓冲：%u 字节（%s）", (unsigned)s_log_size,
             s_log_size == LOG_RAM_SIZE ? "PSRAM" : "内部RAM");

    /* WiFi 只依赖 NVS，在 LCD/摄像头吃内部 DMA 内存之前初始化；连接在事件回调异步进行 */
    fw_wifi_start();

    /* 传感器（MAX30102 / SHT30）*/
    band_sensors_init();

    /* LCD + 触摸 + LVGL 界面 */
    lcd_touch_ui_init();

    /* 手环应用装配：存储 / 串口 / 时钟 三个 HAL */
    s_storage.ctx          = NULL;
    s_storage.read         = storage_read;
    s_storage.program      = storage_program;
    s_storage.erase_sector = storage_erase_sector;
    s_storage.size_bytes   = s_log_size;
    s_storage.sector_size  = 0;                 /* 0 = RAM 模式 */

    s_uart.ctx  = NULL;
    s_uart.send = uart_send_adapter;

    s_clock.ctx       = NULL;
    s_clock.epoch_now = clk_epoch_now;
    s_clock.set_epoch = clk_set_epoch;
    s_clock.millis    = clk_millis;

    fw_band_app_init(&s_app, &s_storage, &s_uart, &s_clock);
    fw_band_app_set_sample_source(&s_app, sample_callback, NULL);

    /* 下行帧路由：0x09 刷新 PC 地址、0x0B 触发拍照、0x0A 仅记日志 */
    {
        const fw_host_hooks_t hooks = {
            .on_capture  = on_capture_cmd,
            .on_wifi_cfg = on_wifi_cfg_cmd,
            .on_motor    = on_motor_cmd,
            .ctx         = NULL,
        };
        fw_host_init(&s_host, &hooks);
    }

    /* 摄像头（失败不阻塞启动；无摄像头时 0x0B 会被明确拒绝并打日志） */
    camera_init();

    /* 任务 */
    xTaskCreate(app_tick_task,   "app_tick", 4096, NULL, 6, NULL);
    xTaskCreate(uart_rx_task,    "uart_rx",  4096, NULL, 5, NULL);
    xTaskCreate(camera_task,     "camera",   4096, NULL, 4, NULL);
    xTaskCreate(net_status_task, "net_stat", 3072, NULL, 3, NULL);
    xTaskCreate(ui_task,         "ui",       4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "任务已启动，等待 PC 校时、0x0B 拍照与图像通道建立");
}
