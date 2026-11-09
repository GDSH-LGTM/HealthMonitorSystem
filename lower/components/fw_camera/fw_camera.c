#include "fw_camera.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_camera.h"

#include "app_config.h"

static const char *TAG = "fw_camera";

static int         s_ready;
static char        s_last_err[96];

/* 摄像头配置（field 顺序必须与 camera_config_t 一致，故用指定初始化器） */
static camera_config_t make_config(const fw_camera_cfg_t *c, int fb_count, int fb_loc)
{
    camera_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.pin_pwdn     = c->pin_pwdn;
    cfg.pin_reset    = c->pin_reset;
    cfg.pin_xclk     = c->pin_xclk;      /* -1：模块自带晶振 */
    cfg.pin_sccb_sda = c->pin_sccb_sda;
    cfg.pin_sccb_scl = c->pin_sccb_scl;
    cfg.pin_d7       = c->pin_d7;
    cfg.pin_d6       = c->pin_d6;
    cfg.pin_d5       = c->pin_d5;
    cfg.pin_d4       = c->pin_d4;
    cfg.pin_d3       = c->pin_d3;
    cfg.pin_d2       = c->pin_d2;
    cfg.pin_d1       = c->pin_d1;
    cfg.pin_d0       = c->pin_d0;
    cfg.pin_vsync    = c->pin_vsync;
    cfg.pin_href     = c->pin_href;
    cfg.pin_pclk     = c->pin_pclk;

    cfg.xclk_freq_hz = CAM_XCLK_HZ;       /* 板载晶振频率，驱动据此算分频；填0会除零panic */
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;

    cfg.pixel_format = PIXFORMAT_JPEG;   /* 必须 JPEG：RGB565 QVGA = 150KB，超 RAM */
    cfg.frame_size   = FRAMESIZE_QVGA;   /* 240x320，与 LCD 匹配 */
    cfg.jpeg_quality = c->jpeg_quality;
    cfg.fb_count     = fb_count;
    cfg.fb_location  = (camera_fb_location_t)fb_loc;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;
    return cfg;
}

int fw_camera_init(const fw_camera_cfg_t *cfg)
{
    size_t psram = esp_psram_get_size();
    int    have_psram = (psram > 0);
    int    fb_count   = have_psram ? 2 : 1;
    int    fb_loc     = have_psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    esp_err_t err;

    s_ready = 0;
    s_last_err[0] = '\0';

    if (cfg == NULL) {
        snprintf(s_last_err, sizeof(s_last_err), "cfg is NULL");
        return -1;
    }

    ESP_LOGI(TAG, "初始化 ATK-OV2640（XCLK 不接，内置 12MHz 晶振；"
                  "%s，fb_count=%d）",
             have_psram ? "帧缓冲放 PSRAM" : "无 PSRAM，帧缓冲放 DRAM", fb_count);

    {
        camera_config_t c = make_config(cfg, fb_count, fb_loc);
        err = esp_camera_init(&c);
    }

    /* PSRAM 分配失败时退到 DRAM 再试一次 */
    if (err != ESP_OK && have_psram) {
        ESP_LOGW(TAG, "PSRAM 帧缓冲失败(%s)，改用 DRAM 重试", esp_err_to_name(err));
        {
            camera_config_t c = make_config(cfg, 1, CAMERA_FB_IN_DRAM);
            err = esp_camera_init(&c);
        }
    }

    if (err != ESP_OK) {
        snprintf(s_last_err, sizeof(s_last_err), "esp_camera_init: %s",
                 esp_err_to_name(err));
        ESP_LOGE(TAG, "摄像头初始化失败：%s", s_last_err);
        ESP_LOGE(TAG, "  检查：D0-D7=4-11, VSYNC=12, HREF=13, PCLK=14, "
                      "SCCB=16/17, PWDN=18, RESET=47");
        return -1;
    }

    /* 输出实际配置，便于对照 */
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
            ESP_LOGI(TAG, "传感器 PID=0x%04X，输出 JPEG QVGA（quality=%d）",
                     s->id.PID, cfg->jpeg_quality);
        }
    }

    s_ready = 1;
    ESP_LOGI(TAG, "摄像头就绪");
    return 0;
}

int fw_camera_is_ready(void)
{
    return s_ready;
}

int fw_camera_capture(const uint8_t **data, uint32_t *len, void **out_frame)
{
    camera_fb_t *fb;

    if (!s_ready) {
        snprintf(s_last_err, sizeof(s_last_err), "camera not ready");
        return -1;
    }
    if (data == NULL || len == NULL || out_frame == NULL) {
        snprintf(s_last_err, sizeof(s_last_err), "null out param");
        return -1;
    }

    fb = esp_camera_fb_get();
    if (fb == NULL) {
        snprintf(s_last_err, sizeof(s_last_err), "esp_camera_fb_get returned NULL");
        ESP_LOGW(TAG, "采集失败：%s", s_last_err);
        return -1;
    }
    if (fb->format != PIXFORMAT_JPEG || fb->len == 0) {
        snprintf(s_last_err, sizeof(s_last_err),
                 "unexpected format=%d len=%u", (int)fb->format, (unsigned)fb->len);
        ESP_LOGW(TAG, "采集数据异常：%s", s_last_err);
        esp_camera_fb_return(fb);
        return -1;
    }

    *data      = fb->buf;
    *len       = (uint32_t)fb->len;
    *out_frame = fb;          /* 原样交回，避免依赖 camera_fb_t 的内部布局 */
    return 0;
}

void fw_camera_return_frame(void *frame)
{
    if (frame == NULL) return;
    esp_camera_fb_return((camera_fb_t *)frame);
}

const char *fw_camera_last_error(void)
{
    return s_last_err;
}
