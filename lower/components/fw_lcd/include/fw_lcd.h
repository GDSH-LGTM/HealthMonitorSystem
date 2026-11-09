#ifndef FW_LCD_H
#define FW_LCD_H

#include <stdint.h>

/* ILI9341 (240x320, SPI) 显示驱动
 * 引脚由调用者传入，本组件不依赖 app_config.h（板级配置集中在 main） */

typedef struct {
    int      spi_host;     /* 如 SPI3_HOST */
    int      pin_sclk;
    int      pin_mosi;
    int      pin_miso;     /* -1 = 不接 */
    int      pin_cs;
    int      pin_dc;
    int      pin_rst;
    uint32_t spi_freq_hz;
    uint16_t width;
    uint16_t height;
} fw_lcd_pins_t;

/* 初始化 SPI 与面板；成功返回 0 */
int  fw_lcd_init(const fw_lcd_pins_t *pins);

/* 启动期加固：把 ESP32-S3 的 strapping 脚（GPIO0/3/45/46）显式上拉。
 *
 * 为什么需要：本设计的 LCD_DC=GPIO3、TOUCH_CS=GPIO45、TOUCH_IRQ=GPIO46
 * 全是 strapping 脚。ROM 在复位瞬间就采样它们决定启动模式 ——
 * 而 LCD 模块的 DC 网络或触摸片的片选网络可能在复位期间把这几个脚拉低，
 * 导致进 JTAG 模式或选错 flash 电压，表现为"上电不启动/时好时坏"。
 *
 * 注意：上电复位时芯片内部的弱上拉本身就要把它们抬到高电平，正常情况下
 * 不需要额外干预（本板当前启动模式 boot:0xc=SPI_FAST_FLASH_BOOT 说明目前是好的）。
 * 本函数的作用是把这件事变成**确定的**，并消除该脚复位期间的下拉。
 * 必须在任何可能驱动这些脚的驱动初始化（fw_lcd_init / fw_touch_init）之前调用。 */
void fw_lcd_harden_straps(void);

/* 面板尺寸 */
uint16_t fw_lcd_width(void);
uint16_t fw_lcd_height(void);

/* 基础绘制 */
void fw_lcd_fill(uint16_t color);
void fw_lcd_fill_area(int x, int y, int w, int h, uint16_t color);

/* LVGL flush 用：把一段 RGB565 像素推到窗口区域 */
void fw_lcd_push(int x, int y, int w, int h, const uint16_t *px, int count);

/* 红屏自检（确认接线与初始化正确） */
void fw_lcd_self_test(void);

#endif /* FW_LCD_H */
