#include "hx711.h"
#include "scale_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "hx711";

/* SCK 高/低电平各保持 ~1us，落在 Datasheet 要求的 0.2~50us 窗口内 */
#define HX711_SCK_DELAY_US 1

int hx711_init(hx711_t *h, int pin_dout, int pin_sck)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << pin_dout),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,    /* DOUT 是开漏，需上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << pin_sck),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&in) != ESP_OK || gpio_config(&out) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败 (DOUT=%d SCK=%d)", pin_dout, pin_sck);
        return -1;
    }

    h->pin_dout = pin_dout;
    h->pin_sck  = pin_sck;
    if (h->gain_pulses < 1 || h->gain_pulses > 3) h->gain_pulses = 1;
    if (h->ready_timeout_ms <= 0) h->ready_timeout_ms = HX711_READY_TIMEOUT_MS;

    gpio_set_level(pin_sck, 0);

    /* 上电后 DOUT 会保持高约 400ms 才出第一个数据 */
    vTaskDelay(pdMS_TO_TICKS(HX711_POWER_UP_MS));

    ESP_LOGI(TAG, "初始化完成 (DOUT=%d SCK=%d gain_pulses=%d)",
             pin_dout, pin_sck, h->gain_pulses);
    return 0;
}

int hx711_is_ready(const hx711_t *h)
{
    return gpio_get_level(h->pin_dout) == 0;
}

int hx711_read_raw(hx711_t *h, int32_t *raw)
{
    uint32_t value = 0;
    int64_t  deadline;
    int      i;

    if (raw == NULL) return -1;

    /* 等 DOUT 拉低 = 数据就绪 */
    deadline = esp_timer_get_time() + (int64_t)h->ready_timeout_ms * 1000;
    while (!hx711_is_ready(h)) {
        if (esp_timer_get_time() > deadline) {
            /* 日志限流：采样任务每 80ms 调一次本函数，HX711 缺失时不加限制
             * 会以 12.5 条/秒刷屏。只在首次与每 100 次打印一次。 */
            ++h->timeout_count;
            if (h->timeout_count == 1 || (h->timeout_count % 100) == 0) {
                ESP_LOGW(TAG, "等 DOUT 拉低超时（%d ms，累计 %u 次）"
                              "——检查 HX711 供电与接线",
                         h->ready_timeout_ms, (unsigned)h->timeout_count);
            }
            return -1;
        }
        esp_rom_delay_us(200);
    }

    /* 24 位，MSB 先出：先读位，再给上升沿 */
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    for (i = 0; i < 24; ++i) {
        gpio_set_level(h->pin_sck, 1);
        esp_rom_delay_us(HX711_SCK_DELAY_US);
        value = (value << 1) | (uint32_t)(gpio_get_level(h->pin_dout) & 0x01);
        gpio_set_level(h->pin_sck, 0);
        esp_rom_delay_us(HX711_SCK_DELAY_US);
    }
    /* 额外脉冲选通道/增益：1 → A@128（默认） */
    for (i = 0; i < h->gain_pulses; ++i) {
        gpio_set_level(h->pin_sck, 1);
        esp_rom_delay_us(HX711_SCK_DELAY_US);
        gpio_set_level(h->pin_sck, 0);
        esp_rom_delay_us(HX711_SCK_DELAY_US);
    }
    portEXIT_CRITICAL(&mux);

    /* 24 位补码 → 有符号 32 位 */
    if (value & 0x00800000u) {
        value |= 0xFF000000u;
    }
    *raw = (int32_t)value;
    return 0;
}

/* 简单插入排序取中位数（samples 很小，够用且不依赖堆） */
static int32_t median_of(int32_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; ++i) {
        int32_t key = a[i];
        for (j = i - 1; j >= 0 && a[j] > key; --j) {
            a[j + 1] = a[j];
        }
        a[j + 1] = key;
    }
    return a[n / 2];
}

int hx711_read_median(hx711_t *h, int samples, int32_t *raw)
{
    static int32_t buf[HX711_MEDIAN_MAX];
    int i, n = 0;

    if (raw == NULL) return -1;
    if (samples < 1) samples = 1;
    if (samples > HX711_MEDIAN_MAX) samples = HX711_MEDIAN_MAX;

    for (i = 0; i < samples; ++i) {
        int32_t v;
        if (hx711_read_raw(h, &v) == 0) {
            buf[n++] = v;
        }
        vTaskDelay(pdMS_TO_TICKS(2));   /* 给 HX711 出新样本的时间（10Hz 时约 100ms） */
    }
    if (n == 0) return -1;
    *raw = median_of(buf, n);
    return 0;
}

int hx711_tare(hx711_t *h, int samples)
{
    int32_t raw;

    if (hx711_read_median(h, samples, &raw) != 0) {
        ESP_LOGE(TAG, "去皮失败：读不到数据");
        return -1;
    }
    h->offset = raw;
    ESP_LOGI(TAG, "去皮完成：offset = %ld", (long)raw);
    return 0;
}

int hx711_calibrate(hx711_t *h, float known_grams, int samples)
{
    int32_t raw;

    if (known_grams <= 0.0f) {
        ESP_LOGE(TAG, "标定失败：砝码重量必须 > 0");
        return -1;
    }
    if (hx711_read_median(h, samples, &raw) != 0) {
        ESP_LOGE(TAG, "标定失败：读不到数据");
        return -1;
    }

    {
        float delta = (float)(raw - h->offset);
        if (delta == 0.0f) {
            ESP_LOGE(TAG, "标定失败：放上砝码后读数没变化（delta=0）");
            return -1;
        }
        h->scale = known_grams / delta;
    }

    ESP_LOGI(TAG, "标定完成：raw=%ld offset=%ld → scale=%.6f g/LSB",
             (long)raw, (long)h->offset, (double)h->scale);
    return 0;
}

float hx711_get_grams(const hx711_t *h, int32_t raw)
{
    if (h->scale <= 0.0f) return 0.0f;
    return (float)(raw - h->offset) * h->scale;
}

void hx711_get_calibration(const hx711_t *h, int32_t *offset, float *scale)
{
    if (offset) *offset = h->offset;
    if (scale)  *scale  = h->scale;
}

void hx711_set_calibration(hx711_t *h, int32_t offset, float scale)
{
    h->offset = offset;
    h->scale  = scale;
    ESP_LOGI(TAG, "恢复标定：offset=%ld scale=%.6f", (long)offset, (double)scale);
}
