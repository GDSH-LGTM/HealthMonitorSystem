/* ESP32 体重秤 —— 主程序
 *
 * 数据链路：
 *   HX711（24 位称重 ADC）→ 采样 → 稳定判定 → 0x02 体重帧 → PC
 *
 * 协议：protocol_spec1.2.md §3 / §4.2
 *   0x02 体重数据帧  秤→PC  载荷 2 字节 = 体重 ×100（0.01kg），0xFFFF 无效
 *   整帧 13 字节；上报时机 = 称重稳定后发 1 帧；秤不缓存、不补传
 *
 * 标定：首次使用必须去皮 + 砝码标定，走 UART0 串口命令（见 docs/CALIBRATION.md）：
 *   t        去皮（把当前读数记为零点）
 *   c<克数>  用已知砝码标定，例如 c500 表示放了 500g 砝码
 *   r        打印原始 ADC 值（排查接线/漂移）
 *   s        状态（重量、是否已标定、连接、统计）
 *   h        帮助 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"

#include "scale_config.h"
#include "fw_proto.h"
#include "scale_frame.h"
#include "fw_scale_app.h"
#include "scale_transport.h"

static const char *TAG = "main";

static fw_scale_app_t s_scale;

/* 设备软时钟：秤没有校时流程（协议 §3 的 0x03 只发给手环），
 * 用开机以来的秒数，0 表示未校时。PC 侧会按接收时刻兜底（协议 §7）。 */
static uint32_t mono_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* ==================== 采样与上报 ==================== */
static void scale_task(void *arg)
{
    uint8_t frame[FW_WEIGHT_FRAME];
    (void)arg;

    for (;;) {
        if (fw_scale_app_tick(&s_scale)) {
            /* 稳定且达到可上报重量 → 发一帧。
             * 帧组装用纯逻辑模块（可在 PC 上自测），未标定时载荷填 0xFFFF */
            float grams = fw_scale_app_stable_grams(&s_scale);
            int n = scale_build_weight_frame(frame, grams, mono_sec(),
                                             fw_scale_app_calibrated(&s_scale));
            if (scale_transport_connected()) {
                if (scale_transport_send(frame, (uint32_t)n)) {
                    ++s_scale.stat_frames;
                    ESP_LOGI(TAG, "已上报体重 %.2f kg（0x02 帧 %d 字节）",
                             (double)(grams / 1000.0f), n);
                } else {
                    ESP_LOGW(TAG, "上报失败（通道断开）");
                }
            } else {
                ESP_LOGW(TAG, "通道未连接，本次称重不重发（协议 §5：秤不缓存）");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ==================== 状态打印 ==================== */
static void print_status(void)
{
    char ip[16];
    int32_t offset;
    float   scale;

    hx711_get_calibration(&s_scale.hx, &offset, &scale);
    scale_transport_pc_ip_str(ip, sizeof(ip));

    ESP_LOGI(TAG, "──────── 状态 ────────");
    ESP_LOGI(TAG, "重量      : %.1f g (%.3f kg)%s",
             (double)fw_scale_app_grams(&s_scale),
             (double)(fw_scale_app_grams(&s_scale) / 1000.0f),
             s_scale.calibrated ? "" : "   ← 未标定，值不可信");
    ESP_LOGI(TAG, "标定      : offset=%ld  scale=%.6f g/LSB",
             (long)offset, (double)scale);
    ESP_LOGI(TAG, "稳定      : %s (连续命中 %d/%d)",
             s_scale.stable ? "是" : "否",
             s_scale.stable_hits, SCALE_STABLE_REQUIRED);
    ESP_LOGI(TAG, "WiFi      : %s", scale_transport_wifi_ready() ? "已连接" : "未连接");
    ESP_LOGI(TAG, "TCP 通道  : %s (PC %s:%d)",
             scale_transport_connected() ? "已连接" : "未连接", ip, SCALE_TCP_PORT);
    ESP_LOGI(TAG, "统计      : 采样 %u 次 / 读数失败 %u 次 / 已上报 %u 帧 / 重连 %u 次",
             (unsigned)s_scale.stat_samples, (unsigned)s_scale.stat_read_err,
             (unsigned)scale_transport_stat_frames(),
             (unsigned)scale_transport_stat_reconnects());
}

/* ==================== 串口标定命令 ==================== */
static void console_task(void *arg)
{
    char line[48];
    int  len = 0;
    (void)arg;

    ESP_LOGI(TAG, "串口命令就绪：'h' 查看帮助");

    for (;;) {
        uint8_t c;
        int r = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100));
        if (r != 1) continue;

        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            line[len] = '\0';
            len = 0;

            switch (line[0]) {
            case 'h':
            case 'H':
                ESP_LOGI(TAG, "命令：t=去皮  c<克数>=砝码标定(如 c500)  r=读原始值  s=状态");
                ESP_LOGI(TAG, "标定流程：空载按 t → 放砝码按 c<克数> → 按 s 确认");
                break;

            case 't':
            case 'T':
                ESP_LOGI(TAG, "去皮中（请确保秤台空载）…");
                if (fw_scale_app_tare(&s_scale) == 0) {
                    ESP_LOGI(TAG, "去皮成功");
                } else {
                    ESP_LOGE(TAG, "去皮失败：读不到 HX711 数据（查接线/供电）");
                }
                break;

            case 'c':
            case 'C': {
                float grams = (float)atof(line + 1);
                if (grams <= 0.0f) {
                    ESP_LOGE(TAG, "用法：c<克数>，例如 c500 表示 500g 砝码");
                    break;
                }
                ESP_LOGI(TAG, "标定中（请已放好 %.1f g 砝码）…", (double)grams);
                if (fw_scale_app_calibrate(&s_scale, grams) == 0) {
                    ESP_LOGI(TAG, "标定成功，已存 NVS");
                    print_status();
                } else {
                    ESP_LOGE(TAG, "标定失败（读不到数据，或放砝码前后读数无变化）");
                }
                break;
            }

            case 'r':
            case 'R': {
                /* 打印原始值，不看标定 —— 用于确认接线与漂移 */
                for (int i = 0; i < 5; ++i) {
                    int32_t raw = 0;
                    if (hx711_read_median(&s_scale.hx, 5, &raw) == 0) {
                        ESP_LOGI(TAG, "原始值 #%d: %ld   (克≈%.1f)",
                                 i + 1, (long)raw,
                                 (double)hx711_get_grams(&s_scale.hx, raw));
                    } else {
                        ESP_LOGE(TAG, "原始值 #%d: 读取失败", i + 1);
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                break;
            }

            case 's':
            case 'S':
                print_status();
                break;

            default:
                ESP_LOGW(TAG, "未知命令 '%c'（按 h 看帮助）", line[0]);
                break;
            }
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = (char)c;
        } else {
            len = 0;    /* 行太长，丢弃 */
        }
    }
}

/* ==================== main ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s 启动", SCALE_FW_NAME, SCALE_FW_VERSION);

    /* UART0 用于日志 + 标定命令。这里只装接收驱动（日志已用 console 发送） */
    {
        const uart_config_t cfg = {
            .baud_rate  = 115200,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
    }

    /* 秤：HX711 + NVS 标定值 */
    if (fw_scale_app_init(&s_scale) != 0) {
        ESP_LOGE(TAG, "HX711 初始化失败（DOUT=%d SCK=%d）—— 检查接线与供电",
                 SCALE_HX711_PIN_DOUT, SCALE_HX711_PIN_SCK);
        /* 不return：仍起任务，便于串口命令排查 */
    }

    /* 上行通道（默认 WiFi TCP，端口 %d） */
    scale_transport_start();

    xTaskCreate(scale_task,   "scale",   4096, NULL, 6, NULL);
    xTaskCreate(console_task, "console", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "任务已启动。未标定请先用串口命令 t / c<克数>（按 h 看帮助）");
    if (!s_scale.calibrated) {
        ESP_LOGW(TAG, "当前未标定，0x02 帧会填 0xFFFF（无效值）");
    }
}
