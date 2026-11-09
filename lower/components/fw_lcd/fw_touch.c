#include "fw_touch.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "fw_touch";

static spi_device_handle_t s_spi;
static fw_touch_cfg_t      s_cfg;

/* XPT2046 命令字节：[start=1][A2A1A0][mode=1 差分][PD1PD0]
 * 差分模式读 X=0x90 / Y=0xD0（含 8 个时钟的采样期） */
#define XPT_CMD_READ_X  0x90
#define XPT_CMD_READ_Y  0xD0

static uint16_t xpt_transfer(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t = {0};
    t.length    = 3 * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    gpio_set_level(s_cfg.pin_cs, 0);
    /* XPT2046 要求命令与数据之间插入若干时钟；3 字节传输天然提供了采样时间 */
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(s_cfg.pin_cs, 1);

    /* 12 位结果位于第 2、3 字节：byte1 低 4 位 + byte2 高 8 位 */
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3) & 0x0FFF;
}

bool fw_touch_read_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    /* IRQ 为低表示有按下（若接了 IRQ） */
    if (s_cfg.pin_irq >= 0 && gpio_get_level(s_cfg.pin_irq) != 0) {
        return false;
    }

    uint16_t x = xpt_transfer(XPT_CMD_READ_X);
    uint16_t y = xpt_transfer(XPT_CMD_READ_Y);

    /* 全 0 或全 1 视为无效（没按或松手） */
    if ((x == 0 && y == 0) || (x >= 0x0FF0 && y >= 0x0FF0)) {
        return false;
    }

    if (raw_x) *raw_x = x;
    if (raw_y) *raw_y = y;
    return true;
}

int fw_touch_init(const fw_touch_cfg_t *cfg)
{
    if (cfg == NULL) return -1;
    s_cfg = *cfg;

    gpio_config_t io = {0};
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    io.pin_bit_mask = (1ULL << s_cfg.pin_cs);
    gpio_config(&io);
    gpio_set_level(s_cfg.pin_cs, 1);

    if (s_cfg.pin_irq >= 0) {
        gpio_config_t irq = {0};
        irq.mode         = GPIO_MODE_INPUT;
        irq.pull_up_en   = GPIO_PULLUP_ENABLE;
        irq.pull_down_en = GPIO_PULLDOWN_DISABLE;
        irq.intr_type    = GPIO_INTR_DISABLE;
        irq.pin_bit_mask = (1ULL << s_cfg.pin_irq);
        gpio_config(&irq);
    }

    spi_device_interface_config_t dev = {0};
    dev.clock_speed_hz = s_cfg.spi_freq_hz;
    dev.mode           = 0;
    dev.spics_io_num   = s_cfg.pin_cs;
    dev.queue_size     = 4;
    esp_err_t err = spi_bus_add_device((spi_host_device_t)s_cfg.spi_host, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XPT2046 挂到 SPI%d 失败: %s",
                 s_cfg.spi_host, esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "XPT2046 初始化完成（SPI%d, CS=%d, IRQ=%d, %u Hz）",
             s_cfg.spi_host, s_cfg.pin_cs, s_cfg.pin_irq,
             (unsigned)s_cfg.spi_freq_hz);
    return 0;
}

/* 原始 ADC → 屏幕坐标 */
static uint16_t scale_axis(uint16_t raw, uint16_t lo, uint16_t hi, uint16_t out_max)
{
    int32_t range, val;
    if (hi <= lo) return 0;
    range = (int32_t)hi - (int32_t)lo;
    val   = (int32_t)raw - (int32_t)lo;
    if (val < 0) val = 0;
    if (val > range) val = range;
    return (uint16_t)((val * (int32_t)out_max) / range);
}

bool fw_touch_read(uint16_t *x, uint16_t *y)
{
    uint16_t rx, ry, sx, sy;

    if (!fw_touch_read_raw(&rx, &ry)) {
        return false;
    }

    if (s_cfg.swap_xy) {
        uint16_t t = rx; rx = ry; ry = t;
    }

    sx = scale_axis(rx, s_cfg.raw_x_min, s_cfg.raw_x_max, (uint16_t)(s_cfg.screen_w - 1));
    sy = scale_axis(ry, s_cfg.raw_y_min, s_cfg.raw_y_max, (uint16_t)(s_cfg.screen_h - 1));

    if (s_cfg.invert_x) sx = (uint16_t)(s_cfg.screen_w - 1 - sx);
    if (s_cfg.invert_y) sy = (uint16_t)(s_cfg.screen_h - 1 - sy);

    if (x) *x = sx;
    if (y) *y = sy;
    return true;
}

void fw_touch_set_calibration(uint16_t x_min, uint16_t x_max,
                              uint16_t y_min, uint16_t y_max)
{
    s_cfg.raw_x_min = x_min;
    s_cfg.raw_x_max = x_max;
    s_cfg.raw_y_min = y_min;
    s_cfg.raw_y_max = y_max;
    ESP_LOGI(TAG, "标定更新: X[%u,%u] Y[%u,%u]", x_min, x_max, y_min, y_max);
}

void fw_touch_set_orientation(bool swap_xy, bool invert_x, bool invert_y)
{
    s_cfg.swap_xy = swap_xy;
    s_cfg.invert_x = invert_x;
    s_cfg.invert_y = invert_y;
    ESP_LOGI(TAG, "方向更新: swap=%d invX=%d invY=%d",
             (int)swap_xy, (int)invert_x, (int)invert_y);
}
