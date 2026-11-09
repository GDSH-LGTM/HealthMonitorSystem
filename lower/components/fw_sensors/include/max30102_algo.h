#ifndef __MAX30102_ALGO_H
#define __MAX30102_ALGO_H

#include <stdint.h>

/* Maxim 官方 MAX30102 心率/血氧算法 (平台无关, 纯C) */

#define MAX30102_ALGO_FS          100
#define MAX30102_ALGO_BUFFER_SIZE (MAX30102_ALGO_FS * 5)  /* 500 samples = 5s */
#define MAX30102_ALGO_MA4_SIZE    4
#define MAX30102_ALGO_HAMMING_SIZE 5

#ifndef min
#define min(x,y) ((x) < (y) ? (x) : (y))
#endif

/*
 * 计算心率和血氧饱和度
 * @param pun_ir_buffer     红外数据缓冲区
 * @param n_ir_buffer_length 缓冲区长度(通常500)
 * @param pun_red_buffer     红光数据缓冲区
 * @param pn_spo2            输出: 血氧值
 * @param pch_spo2_valid     输出: 1=有效
 * @param pn_heart_rate      输出: 心率(BPM)
 * @param pch_hr_valid       输出: 1=有效
 */
void maxim_heart_rate_and_oxygen_saturation(
    uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
    uint32_t *pun_red_buffer,
    int32_t *pn_spo2, int8_t *pch_spo2_valid,
    int32_t *pn_heart_rate, int8_t *pch_hr_valid);

#endif /* __MAX30102_ALGO_H */
