#ifndef FW_TOUCH_H
#define FW_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

/* XPT2046 电阻触摸驱动（与 LCD 共用 SPI 总线，独立 CS）
 *
 * 旧固件里 touch_init/touch_read 是空函数（交接文档标"触摸功能 15%，驱动代码未写"），
 * 本文件是真正的实现。
 *
 * 注意：XPT2046 的 MISO 在 GPIO39（JTAG MTCK），低速 SPI 可用；
 * 高速 LCD SPI 不能用 GPIO39-42（会白屏），所以 LCD 的 MISO 仍为 -1。 */

typedef struct {
    int      spi_host;      /* 必须与 LCD 同一个 host（共用 SCK/MOSI） */
    int      pin_cs;
    int      pin_irq;       /* -1 = 不用中断，靠轮询 */
    uint32_t spi_freq_hz;   /* 触摸建议 <= 1MHz */
    uint16_t screen_w;
    uint16_t screen_h;
    /* 原始 ADC 量程标定（可在运行时改）。默认取 XPT2046 常见范围 */
    uint16_t raw_x_min, raw_x_max;
    uint16_t raw_y_min, raw_y_max;
    bool     swap_xy;       /* X/Y 互换 */
    bool     invert_x;      /* 反向 */
    bool     invert_y;
} fw_touch_cfg_t;

int  fw_touch_init(const fw_touch_cfg_t *cfg);

/* 读一次触摸。返回 true 表示有按下，并回填屏幕坐标（已标定、已裁剪到屏幕内） */
bool fw_touch_read(uint16_t *x, uint16_t *y);

/* 读原始 ADC 值（标定用）：在屏幕上画十字或打日志后据此调 raw_* / invert_* */
bool fw_touch_read_raw(uint16_t *raw_x, uint16_t *raw_y);

/* 运行时调整标定参数（供串口命令或界面交互调用） */
void fw_touch_set_calibration(uint16_t x_min, uint16_t x_max,
                              uint16_t y_min, uint16_t y_max);
void fw_touch_set_orientation(bool swap_xy, bool invert_x, bool invert_y);

#endif /* FW_TOUCH_H */
