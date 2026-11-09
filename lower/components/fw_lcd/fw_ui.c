#include "fw_ui.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl.h"

static const char *TAG = "fw_ui";

/* 每块绘制缓冲 240x20 像素 = 9600B；两块共 19.2KB，放内部 DMA 内存。
 * 不用整屏缓冲（240*320*2 = 150KB），避免依赖 PSRAM。 */
#define UI_DRAW_BUF_LINES 20

/* LVGL tick 基准 */
#define UI_TICK_PERIOD_US 1000

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t         s_buf1[FW_UI_WIDTH * UI_DRAW_BUF_LINES];
static lv_color_t         s_buf2[FW_UI_WIDTH * UI_DRAW_BUF_LINES];
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;

static lv_obj_t *s_lbl_hr;
static lv_obj_t *s_lbl_spo2;
static lv_obj_t *s_lbl_temp;
static lv_obj_t *s_lbl_hum;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_img;

/* 上次刷新的时间戳（自己算节拍，不依赖外部定时器） */
static int64_t s_last_loop_us;
static int64_t s_last_tick_us;

/* ==================== LVGL 显示回调 ==================== */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;

    fw_lcd_push(area->x1, area->y1, w, h, (const uint16_t *)color_p, w * h);
    lv_disp_flush_ready(drv);
}

/* ==================== 触摸输入回调 ==================== */
static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x = 0, y = 0;

    if (fw_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

/* ==================== 界面 ==================== */
static lv_obj_t *make_row(lv_obj_t *parent, const char *title, uint32_t color,
                          int y, lv_obj_t **out_value)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 12, y);

    *out_value = lv_label_create(parent);
    lv_label_set_text(*out_value, "--");
    lv_obj_set_style_text_color(*out_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(*out_value, LV_ALIGN_TOP_RIGHT, -12, y);
    return lbl;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101020), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Health Monitor");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    make_row(scr, "HR",   0xFF4040, 46,  &s_lbl_hr);
    make_row(scr, "SpO2", 0x4080FF, 92,  &s_lbl_spo2);
    make_row(scr, "Temp", 0xFFD040, 138, &s_lbl_temp);
    make_row(scr, "Hum",  0x40FF80, 184, &s_lbl_hum);

    /* 通道状态区 */
    s_lbl_wifi = lv_label_create(scr);
    lv_label_set_text(s_lbl_wifi, "WiFi: --");
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_BOTTOM_LEFT, 12, -30);

    s_lbl_img = lv_label_create(scr);
    lv_label_set_text(s_lbl_img, "Image: --");
    lv_obj_set_style_text_color(s_lbl_img, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_align(s_lbl_img, LV_ALIGN_BOTTOM_LEFT, 12, -8);

    ESP_LOGI(TAG, "界面已创建（240x320，双缓冲 %d 行）", UI_DRAW_BUF_LINES);
}

/* ==================== 初始化 ==================== */
static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(UI_TICK_PERIOD_US / 1000);
}

int fw_ui_init(void)
{
    if (fw_lcd_width() == 0) {
        ESP_LOGE(TAG, "LCD 未初始化，先调用 fw_lcd_init()");
        return -1;
    }

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2,
                          FW_UI_WIDTH * UI_DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = fw_lcd_width();
    s_disp_drv.ver_res  = fw_lcd_height();
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    if (lv_disp_drv_register(&s_disp_drv) == NULL) {
        ESP_LOGE(TAG, "显示驱动注册失败");
        return -1;
    }

    /* 触摸输入（未初始化时也注册，读回调会一直返回 RELEASED） */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = indev_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    build_ui();

    /* FreeRTOS 软件定时器驱动 lv_tick_inc */
    {
        const esp_timer_create_args_t args = {
            .callback = tick_cb,
            .name     = "lv_tick",
        };
        esp_timer_handle_t h;
        if (esp_timer_create(&args, &h) == ESP_OK) {
            esp_timer_start_periodic(h, UI_TICK_PERIOD_US);
        } else {
            ESP_LOGW(TAG, "lv_tick 定时器创建失败");
        }
    }

    s_last_loop_us = esp_timer_get_time();
    s_last_tick_us = s_last_loop_us;
    return 0;
}

void fw_ui_loop(void)
{
    /* lv_timer_handler 建议 5~30ms 一次；这里按 10ms 自己节拍 */
    int64_t now = esp_timer_get_time();
    if (now - s_last_loop_us >= 10000) {
        s_last_loop_us = now;
        lv_timer_handler();
    }
}

/* ==================== 数据更新 ==================== */
void fw_ui_update_vitals(int hr, int spo2, float temp_c, float hum_pct, int hr_state)
{
    char buf[32];
    char mark[4];

    if (s_lbl_hr == NULL) return;

    /* hr_state: 0 正常 / 1 无效 / 2 降级。降级时值可能不准，加个标记。
     * 注意不能把 snprintf 的目标和源都写成 buf（重叠是未定义行为，会被 -Werror=restrict 拦下） */
    mark[0] = '\0';
    if (hr_state == 2) {
        mark[0] = '?';
        mark[1] = '\0';
    }

    if (hr > 0) {
        snprintf(buf, sizeof(buf), "%d BPM%s", hr, mark);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_lbl_hr, buf);

    if (spo2 > 0) {
        snprintf(buf, sizeof(buf), "%d %%", spo2);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_lbl_spo2, buf);

    if (temp_c > -100.0f) {
        snprintf(buf, sizeof(buf), "%.1f C", (double)temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_lbl_temp, buf);

    if (hum_pct >= 0.0f) {
        snprintf(buf, sizeof(buf), "%.1f %%", (double)hum_pct);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_lbl_hum, buf);
}

void fw_ui_update_status(int wifi_ready, int img_connected, const char *pc_ip)
{
    char buf[80];

    if (s_lbl_wifi == NULL) return;

    if (wifi_ready) {
        snprintf(buf, sizeof(buf), "WiFi: OK");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x40FF80), LV_PART_MAIN);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: connecting...");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0xFFA040), LV_PART_MAIN);
    }
    lv_label_set_text(s_lbl_wifi, buf);

    if (img_connected) {
        snprintf(buf, sizeof(buf), "Image: OK %s", pc_ip ? pc_ip : "");
        lv_obj_set_style_text_color(s_lbl_img, lv_color_hex(0x40FF80), LV_PART_MAIN);
    } else {
        snprintf(buf, sizeof(buf), "Image: off %s", pc_ip ? pc_ip : "");
        lv_obj_set_style_text_color(s_lbl_img, lv_color_hex(0xFF6060), LV_PART_MAIN);
    }
    lv_label_set_text(s_lbl_img, buf);
}

void fw_ui_boot_screen(void)
{
    /* LVGL 还没起来，直接刷屏给个反馈（深蓝底） */
    fw_lcd_fill(0x0841);
}
