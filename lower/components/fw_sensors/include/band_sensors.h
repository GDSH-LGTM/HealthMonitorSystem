#ifndef BAND_SENSORS_H
#define BAND_SENSORS_H

#include <stdint.h>
#include "fw_proto.h"

/* 板级外设：MAX30102（心率血氧）/ SHT30（温湿度）/ 马达 / 状态 LED
 *
 * 关键设计（对比旧固件修掉的两个 bug）：
 *  1) MAX30102 是 100sps 配置，必须每 10ms 读一次 FIFO。
 *     旧代码在 1000ms 的显示循环里每次只读 1 个样本 → FIFO 严重溢出、数据全体错乱。
 *     本版用独立 10ms 任务，且一次把 FIFO 里所有可用样本读干（防抖动累积）。
 *  2) SHT30 旧代码用 0x2C 0x06（时钟拉伸）+ 单独 i2c_master_receive，读不到数据。
 *     本版改 0x24 0x00（无时钟拉伸）+ transmit_receive（重复起始）+ CRC8 校验。 */

/* 初始化 I2C 总线与全部外设；找不到的设备只告警不阻塞启动 */
void band_sensors_init(void);

/* 取最新一次有效读数（由 10ms 采样任务在后台更新）。
 * 无效字段按协议填哨兵：hr=0xFFFF / spo2=0xFF / temp=0x7FFF / hum=0xFFFF
 * 注意：sweat / cal / hr_state 由调用方（fw_band_app）覆盖。 */
void band_sensors_read(fw_vital_t *out);

/* 探测 I2C 总线上的设备，返回在位设备数；结果打到日志 */
int  band_sensors_i2c_scan(void);

/* 振动马达 */
void band_motor_init(void);
void band_motor_on(uint16_t duration_ms);
void band_motor_tick(void);

#endif /* BAND_SENSORS_H */
