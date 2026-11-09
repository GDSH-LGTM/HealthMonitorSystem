#include "fw_scale_app.h"
#include "scale_config.h"
#include "fw_proto.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "scale";

/* ==================== NVS 标定持久化 ==================== */
static void load_calibration(fw_scale_app_t *a)
{
    nvs_handle_t h;
    int32_t offset = 0;
    float   scale  = 0.0f;
    size_t  sz     = sizeof(scale);

    if (nvs_open(SCALE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS 里没有标定值 —— 首次使用需先去皮 + 砝码标定");
        a->calibrated = 0;
        return;
    }

    /* NVS 没有原生 float 类型，scale 用 blob 存 */
    if (nvs_get_i32(h, SCALE_NVS_KEY_OFFSET, &offset) != ESP_OK ||
        nvs_get_blob(h, SCALE_NVS_KEY_SCALE, &scale, &sz) != ESP_OK) {
        ESP_LOGW(TAG, "NVS 标定值不完整 —— 需重新标定");
        nvs_close(h);
        a->calibrated = 0;
        return;
    }
    nvs_close(h);

    if (scale <= 0.0f) {
        ESP_LOGW(TAG, "NVS 标定值无效（scale=%.6f）—— 需重新标定", (double)scale);
        a->calibrated = 0;
        return;
    }
    hx711_set_calibration(&a->hx, offset, scale);
    a->calibrated = 1;
}

static void save_calibration(const fw_scale_app_t *a)
{
    nvs_handle_t h;
    int32_t offset;
    float   scale;

    hx711_get_calibration(&a->hx, &offset, &scale);

    if (nvs_open(SCALE_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败，标定值未能保存");
        return;
    }
    esp_err_t e1 = nvs_set_i32(h, SCALE_NVS_KEY_OFFSET, offset);
    esp_err_t e2 = nvs_set_blob(h, SCALE_NVS_KEY_SCALE, &scale, sizeof(scale));
    if (e1 == ESP_OK && e2 == ESP_OK) {
        nvs_commit(h);
        ESP_LOGI(TAG, "标定值已保存到 NVS：offset=%ld scale=%.6f",
                 (long)offset, (double)scale);
    } else {
        ESP_LOGE(TAG, "NVS 写入失败 (%s / %s)", esp_err_to_name(e1), esp_err_to_name(e2));
    }
    nvs_close(h);
}

/* ==================== 初始化 ==================== */
int fw_scale_app_init(fw_scale_app_t *a)
{
    if (a == NULL) return -1;
    memset(a, 0, sizeof(*a));

    a->hx.gain_pulses      = 1;      /* A 通道 @128 增益 */
    a->hx.ready_timeout_ms = HX711_READY_TIMEOUT_MS;

    if (hx711_init(&a->hx, SCALE_HX711_PIN_DOUT, SCALE_HX711_PIN_SCK) != 0) {
        return -1;
    }

    load_calibration(a);

    ESP_LOGI(TAG, "%s v%s 就绪（采样周期 %dms，稳定窗口 %d 样本 / 极差 ≤ %.0fg）",
             SCALE_FW_NAME, SCALE_FW_VERSION,
             SCALE_SAMPLE_PERIOD_MS, SCALE_STABLE_WINDOW,
             (double)SCALE_STABLE_TOLERANCE_G);
    if (!a->calibrated) {
        ESP_LOGW(TAG, "尚未标定：重量将一直为 0。"
                      "串口命令 't' 去皮，'c<克数>' 用砝码标定（详见 docs/CALIBRATION.md）");
    }
    return 0;
}

/* ==================== 稳定判定 ==================== */
static void window_push(fw_scale_app_t *a, float g)
{
    a->win[a->win_pos] = g;
    a->win_pos = (a->win_pos + 1) % SCALE_WIN_MAX;
    if (a->win_count < SCALE_WIN_MAX) ++a->win_count;
}

/* 窗口内极差是否在容差内 */
static int window_is_stable(const fw_scale_app_t *a, float *mean_out)
{
    float mn = 1e9f, mx = -1e9f, sum = 0.0f;
    int   n = a->win_count, i;

    if (n < SCALE_STABLE_WINDOW) return 0;

    /* 只取最近 SCALE_STABLE_WINDOW 个 */
    for (i = 0; i < SCALE_STABLE_WINDOW; ++i) {
        int idx = (a->win_pos - 1 - i + SCALE_WIN_MAX) % SCALE_WIN_MAX;
        float v = a->win[idx];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    if (mean_out) *mean_out = sum / (float)SCALE_STABLE_WINDOW;
    return ((mx - mn) <= SCALE_STABLE_TOLERANCE_G) ? 1 : 0;
}

/* ==================== 采样节拍 ==================== */
int fw_scale_app_tick(fw_scale_app_t *a)
{
    int32_t raw;
    float   g;
    float   mean = 0.0f;
    int     report = 0;

    a->ms += 1;
    if (a->ms - a->last_sample_ms < SCALE_SAMPLE_PERIOD_MS) {
        return 0;
    }
    a->last_sample_ms = a->ms;

    if (hx711_read_raw(&a->hx, &raw) != 0) {
        ++a->stat_read_err;
        /* 连续失败不清窗口（可能只是这一拍没就绪），但把稳定计数清掉 */
        a->stable_hits = 0;
        a->stable = 0;
        return 0;
    }
    ++a->stat_samples;

    g = hx711_get_grams(&a->hx, raw);

    /* 未标定：不算稳定，也不上报 */
    if (!a->calibrated) {
        a->grams = 0.0f;
        return 0;
    }

    /* 异常读数丢弃（超量程、负值等） */
    if (g < -SCALE_MAX_GRAMS || g > SCALE_MAX_GRAMS) {
        return 0;
    }

    a->grams = g;
    window_push(a, g);

    if (window_is_stable(a, &mean)) {
        a->grams = mean;
        ++a->stable_hits;
        if (a->stable_hits >= SCALE_STABLE_REQUIRED) {
            a->stable = 1;
            /* 稳定且达到可上报重量，且本次稳定尚未上报过 → 上报一次 */
            if (!a->reported && mean >= SCALE_MIN_GRAMS) {
                a->reported = 1;
                report = 1;
            }
        }
    } else {
        a->stable_hits = 0;
        a->stable = 0;
        /* 离开稳定态 → 允许下次稳定时再报一帧 */
        a->reported = 0;
    }

    /* 重新回到空载（例如把东西拿走）→ 允许下次再报 */
    if (a->grams < SCALE_MIN_GRAMS && a->stable) {
        a->reported = 0;
    }

    return report;
}

float fw_scale_app_grams(const fw_scale_app_t *a)
{
    return a->grams;
}

int fw_scale_app_tare(fw_scale_app_t *a)
{
    if (hx711_tare(&a->hx, 8) != 0) {
        return -1;
    }
    a->win_count = 0;
    a->win_pos = 0;
    a->stable_hits = 0;
    a->stable = 0;
    a->reported = 0;
    save_calibration(a);
    return 0;
}

int fw_scale_app_calibrate(fw_scale_app_t *a, float known_grams)
{
    if (hx711_calibrate(&a->hx, known_grams, 8) != 0) {
        return -1;
    }
    a->calibrated = 1;
    a->win_count = 0;
    a->win_pos = 0;
    a->stable_hits = 0;
    a->stable = 0;
    a->reported = 0;
    save_calibration(a);
    return 0;
}

/* ==================== 组帧 ==================== */
/* 帧的字节组装在 scale_frame.c（纯逻辑，可在 PC 上自测）。
 * 这里只提供应用层状态查询。 */

int fw_scale_app_calibrated(const fw_scale_app_t *a)
{
    return a->calibrated;
}

float fw_scale_app_stable_grams(const fw_scale_app_t *a)
{
    return a->grams;
}
