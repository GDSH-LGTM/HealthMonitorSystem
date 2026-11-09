#ifndef HX711_H
#define HX711_H

#include <stdint.h>

/* HX711 24 位称重 ADC 驱动（GPIO bit-bang）
 *
 * ⚠️ HX711 不是标准 SPI：数据线 DOUT 在数据就绪时被拉低，之后每个 SCK 上升沿
 *    移出一位（MSB 先出），共需 **25 个脉冲**（24 位数据 + 1 个脉冲选择 A 通道
 *    增益 128）。所以不能用 esp_driver_spi，只能自己拉 GPIO。
 *
 * 时序（Datasheet）：
 *   - DOUT 低 → 数据就绪
 *   - SCK 高电平需 0.2~50us；低电平需 0.2us 以上
 *   - 第 25 个脉冲后 DOUT 回到高，芯片进入掉电模式，直到下次数据就绪
 *   - 上电/复位后 DOUT 会保持高约 400ms 才出第一个数据
 *
 * 读数是有符号 24 位补码。空载受温度/装配影响会漂移，所以必须先去皮(tare)、
 * 再用已知砝码标定(calibrate)。 */

typedef struct {
    int      pin_dout;      /* 数据（输入） */
    int      pin_sck;       /* 时钟（输出） */
    int      gain_pulses;   /* 额外脉冲数：1=A@128（默认）, 2=B@32, 3=A@64 */
    int32_t  offset;        /* 零点原始值（去皮得到） */
    float    scale;         /* 每单位原始值对应的克数，calibrate() 计算 */
    int      ready_timeout_ms;  /* 等 DOUT 拉低的最长时间 */

    /* 读取超时次数。仅用于日志限流：采样任务每 80ms 调一次 read，
     * 若 HX711 没接/没供电，不加限制会以 12.5 条/秒的速率刷屏，
     * 把其它日志全冲掉。计数器也顺便给调用方当健康指标用。 */
    uint32_t timeout_count;
} hx711_t;

/* 初始化 GPIO。返回 0 成功 */
int  hx711_init(hx711_t *h, int pin_dout, int pin_sck);

/* 1 = 有数据可读（DOUT 已拉低） */
int  hx711_is_ready(const hx711_t *h);

/* 读一次原始值。成功返回 0 并回填 raw（已符号扩展）。
 * 超时（DOUT 一直不低，通常是没接线或没供电）返回 -1 */
int  hx711_read_raw(hx711_t *h, int32_t *raw);

/* 连续读 count 次取中位数，抗脉冲干扰。成功返回 0 */
int  hx711_read_median(hx711_t *h, int samples, int32_t *raw);

/* 去皮：把当前读数记为零点 */
int  hx711_tare(hx711_t *h, int samples);

/* 标定：放上已知重量的砝码后调用。known_grams 为砝码克数 */
int  hx711_calibrate(hx711_t *h, float known_grams, int samples);

/* 取重量（克）。未标定（scale<=0）返回 0 */
float hx711_get_grams(const hx711_t *h, int32_t raw);

/* 当前标定参数（写入 NVS 前用） */
void hx711_get_calibration(const hx711_t *h, int32_t *offset, float *scale);
/* 从 NVS 恢复标定参数 */
void hx711_set_calibration(hx711_t *h, int32_t offset, float scale);

#endif /* HX711_H */
