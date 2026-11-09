#include "fw_lcd.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "fw_lcd";

static spi_device_handle_t s_spi;
static fw_lcd_pins_t       s_pins;
static uint16_t            s_w, s_h;

/* ==================== 底层写 ==================== */
static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(s_pins.pin_dc, 0);
    spi_transaction_t t = {0};
    t.length    = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    if (len <= 0) return;
    gpio_set_level(s_pins.pin_dc, 1);

    /* 大块数据分片发，避免单次事务过长 */
    const int kChunk = 2048;
    int off = 0;
    while (off < len) {
        int n = (len - off > kChunk) ? kChunk : (len - off);
        spi_transaction_t t = {0};
        t.length    = n * 8;
        t.tx_buffer = data + off;
        spi_device_polling_transmit(s_spi, &t);
        off += n;
    }
}

static void lcd_data_byte(uint8_t b)
{
    lcd_data(&b, 1);
}

/* ==================== 面板初始化序列 ==================== */
static void panel_init(void)
{
    /* 硬件复位 */
    if (s_pins.pin_rst >= 0) {
        gpio_set_level(s_pins.pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(s_pins.pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(s_pins.pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    lcd_cmd(0x01);                       /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x11);                       /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x3A);                       /* COLMOD: 16bit/pixel (RGB565) */
    lcd_data_byte(0x55);

    lcd_cmd(0x36);                       /* MADCTL: 竖屏，BGR */
    lcd_data_byte(0x48);

    lcd_cmd(0xB1);                       /* FRMCTR1 */
    { uint8_t d[2] = {0x00, 0x18}; lcd_data(d, 2); }

    lcd_cmd(0xB6);                       /* DFUNCTR */
    { uint8_t d[3] = {0x0A, 0xA2, 0x27}; lcd_data(d, 3); }

    lcd_cmd(0xC0);                       /* PWCTR1 */
    lcd_data_byte(0x10);

    lcd_cmd(0xC1);                       /* PWCTR2 */
    lcd_data_byte(0x10);

    lcd_cmd(0xC5);                       /* VMCTR1 */
    { uint8_t d[2] = {0x3E, 0x28}; lcd_data(d, 2); }

    lcd_cmd(0xC7);                       /* VMCTR2 */
    lcd_data_byte(0x86);

    lcd_cmd(0x21);                       /* INVON：多数 ILI9341 模块需要反色 */

    lcd_cmd(0x36);
    lcd_data_byte(0x48);

    lcd_cmd(0x29);                       /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ==================== 启动期 strapping 加固 ==================== */
/* 见 fw_lcd.h 的说明。ESP32-S3 的 strapping 脚是 GPIO0/3/45/46，ROM 在硬件复位
 * 瞬间采样它们：
 *   GPIO0  低 = 进入下载模式
 *   GPIO3  JTAG 源选择
 *   GPIO45 VDD_SPI 电压选择
 *   GPIO46 ROM 启动相关
 * 本设计把 LCD_DC=3、TOUCH_CS=45、TOUCH_IRQ=46 都放在这些脚上，所以存在
 * "外设网络在复位期间把 strapping 脚拉低导致启动异常"的硬件风险。
 *
 * 老实说清楚这个函数能做什么、不能做什么：
 *   不能 —— 采样发生在硬件复位那一刻，软件在这之后才运行，所以它**修不了本次启动**。
 *          若载板真的把 GPIO3 强下拉，固件也无能为力，只能改硬件（加/去掉上下拉）。
 *   能做 —— 把这三个脚的初始状态显式定为"输入 + 上拉"，而不是听任复位默认值，
 *          从而在**后续复位**时消除该脚被拉低的机会；同时也把这处硬件风险写进代码。
 *
 * 只设方向为输入 + 上拉，不设下拉。这三个脚在驱动初始化之前本来就是输入态
 * （LCD_DC 与 TOUCH_CS 随后由各自 init 改成输出并拉到高，TOUCH_IRQ 一直是输入），
 * 所以本调用不改变最终引脚形态，只让启动期的状态确定。 */
void fw_lcd_harden_straps(void)
{
    static const int straps[] = { 0, 3, 45, 46 };
    int done = 0;

    for (int i = 0; i < (int)(sizeof(straps) / sizeof(straps[0])); ++i) {
        gpio_num_t pin = (gpio_num_t)straps[i];

        /* 先设下拉为"无"，再设上拉：避免中间出现下拉生效的窗口 */
        gpio_set_pull_mode(pin, GPIO_FLOATING);
        if (gpio_set_direction(pin, GPIO_MODE_INPUT) != ESP_OK) continue;
        if (gpio_input_enable(pin) != ESP_OK) continue;
        if (gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY) != ESP_OK) continue;
        ++done;
    }

    ESP_LOGI(TAG, "strapping 加固：GPIO0/3/45/46 设为输入+上拉（%d/%d 成功）",
             done, (int)(sizeof(straps) / sizeof(straps[0])));
}

/* ==================== 公开接口 ==================== */
int fw_lcd_init(const fw_lcd_pins_t *pins)
{
    if (pins == NULL) return -1;
    s_pins = *pins;
    s_w = pins->width  ? pins->width  : 240;
    s_h = pins->height ? pins->height : 320;

    /* GPIO */
    gpio_config_t io = {0};
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    io.pin_bit_mask = (1ULL << s_pins.pin_cs) |
                      (1ULL << s_pins.pin_dc) |
                      (1ULL << s_pins.pin_rst);
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败");
        return -1;
    }
    gpio_set_level(s_pins.pin_cs, 1);

    /* SPI 总线 */
    spi_bus_config_t bus = {0};
    bus.mosi_io_num   = s_pins.pin_mosi;
    bus.miso_io_num   = s_pins.pin_miso;
    bus.sclk_io_num   = s_pins.pin_sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    /* 240x320 RGB565 一行 = 480 字节，留足 DMA 空间 */
    bus.max_transfer_sz = s_w * 2 * 20;
    esp_err_t err = spi_bus_initialize((spi_host_device_t)s_pins.spi_host, &bus,
                                       SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(err));
        return -1;
    }

    spi_device_interface_config_t dev = {0};
    dev.clock_speed_hz = s_pins.spi_freq_hz;
    dev.mode           = 0;
    dev.spics_io_num   = s_pins.pin_cs;
    dev.queue_size     = 8;
    err = spi_bus_add_device((spi_host_device_t)s_pins.spi_host, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 设备添加失败: %s", esp_err_to_name(err));
        return -1;
    }

    panel_init();

    ESP_LOGI(TAG, "ILI9341 初始化完成 %dx%d  SPI%d %u Hz",
             s_w, s_h, s_pins.spi_host, (unsigned)s_pins.spi_freq_hz);
    return 0;
}

uint16_t fw_lcd_width(void)  { return s_w; }
uint16_t fw_lcd_height(void) { return s_h; }

void fw_lcd_fill_area(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;

    lcd_cmd(0x2A);                       /* CASET */
    { uint8_t d[4] = { (uint8_t)(x >> 8), (uint8_t)x,
                       (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1) };
      lcd_data(d, 4); }

    lcd_cmd(0x2B);                       /* RASET */
    { uint8_t d[4] = { (uint8_t)(y >> 8), (uint8_t)y,
                       (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1) };
      lcd_data(d, 4); }

    lcd_cmd(0x2C);                       /* RAMWR */

    gpio_set_level(s_pins.pin_dc, 1);
    /* 分批填色 */
    static uint8_t line[240 * 2];
    int px = w;
    if (px > (int)s_w) px = s_w;
    for (int i = 0; i < px; ++i) {
        line[i * 2]     = (uint8_t)(color >> 8);
        line[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }
    {
        spi_transaction_t t = {0};
        t.length    = px * 16;
        t.tx_buffer = line;
        for (int r = 0; r < h; ++r) {
            spi_device_polling_transmit(s_spi, &t);
        }
    }
}

void fw_lcd_fill(uint16_t color)
{
    fw_lcd_fill_area(0, 0, s_w, s_h, color);
}

void fw_lcd_push(int x, int y, int w, int h, const uint16_t *px, int count)
{
    (void)count;
    if (w <= 0 || h <= 0 || px == NULL) return;

    lcd_cmd(0x2A);
    { uint8_t d[4] = { (uint8_t)(x >> 8), (uint8_t)x,
                       (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1) };
      lcd_data(d, 4); }

    lcd_cmd(0x2B);
    { uint8_t d[4] = { (uint8_t)(y >> 8), (uint8_t)y,
                       (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1) };
      lcd_data(d, 4); }

    lcd_cmd(0x2C);
    lcd_data((const uint8_t *)px, w * h * 2);
}

void fw_lcd_self_test(void)
{
    ESP_LOGI(TAG, "自检：红 → 绿 → 蓝 → 黑");
    fw_lcd_fill(0xF800); vTaskDelay(pdMS_TO_TICKS(300));
    fw_lcd_fill(0x07E0); vTaskDelay(pdMS_TO_TICKS(300));
    fw_lcd_fill(0x001F); vTaskDelay(pdMS_TO_TICKS(300));
    fw_lcd_fill(0x0000);
    ESP_LOGI(TAG, "自检完成（若屏幕依次红绿蓝则接线与初始化正确）");
}
