#ifndef FW_BAND_APP_H
#define FW_BAND_APP_H

#include <stdint.h>
#include "fw_hal.h"
#include "fw_proto.h"
#include "fw_log.h"

/* 手环应用状态机（纯 C，无板级依赖；板级能力经 fw_hal 注入�?
 *
 * 行为（协�?v1.1 §7 设备端）�?
 *   - �?1000ms 采一帧：ts=软时钟（未校时为 0）、cal=当日累计（占位公式见 .c�?
 *     �?追加进环形日志；链路可用且不在补传中 �?同时实时发�?
 *   - �?0x03：校�?+ 清零当日累计；跨零点（软时钟日界变化）自动清�?
 *   - �?0x05：检索日�?ts>since 的帧 �?0x04(开�?count) �?20ms/帧补�?�?
 *     追上日志尾部�?0x04(结束)；补传期间新采样帧继续写日志、一并补出（边补边追）；
 *     补传中收到新 0x05 忽略；since 之后无帧则静默（不发 0x04�?
 *   - link_up：默�?1；有 HC-05 STATE 引脚时由 set_link 驱动�?
 *     收到任何主机帧视为链路已通（自动�?1�?
 *
 * 调用约定（均在主循环上下文，勿在中断里调）：
 *   USART RX 中断 �?主循环取字节 �?fw_band_app_rx_byte
 *   SysTick / 主循�?1ms �?fw_band_app_tick
 */

typedef void (*fw_sample_fn)(void *ctx, fw_vital_t *out);
/* 采样回调：填 hr/spo2/temp_x10/hum_x10/sweat/hr_state；ts/cal �?app 覆盖 */

typedef struct {
    fw_log_t     log;
    fw_uart_t    uart;
    fw_clock_t   clk;
    fw_sample_fn sample_fn;
    void        *sample_ctx;
    uint32_t last_sample_ms;   /* 采样节拍 */
    uint32_t last_replay_ms;   /* 补传节拍�?0ms/帧） */
    uint32_t last_day;         /* epoch/86400：跨零点检�?*/
    uint32_t calorie;          /* 当日累计 kcal */
    uint8_t  link_up;          /* 1=实时发送；0=只缓�?*/
    uint8_t  replaying;        /* 补传会话�?*/
    uint32_t replay_seq;       /* 下一个要补传的绝对帧序号 */
    uint8_t  rxbuf[FW_HOST_FRAME];   /* 主机帧滑窗（15 字节定长�?*/
    int      rxlen;
    /* 统计（测�?调试用） */
    uint32_t stat_samples;     /* 累计采样帧数 */
    uint32_t stat_live_sent;   /* 累计实时发送帧�?*/
    uint32_t stat_replayed;    /* 本次补传已发帧数 */
} fw_band_app_t;

void fw_band_app_init(fw_band_app_t *app, const fw_storage_t *st,
                      const fw_uart_t *uart, const fw_clock_t *clk);
void fw_band_app_set_sample_source(fw_band_app_t *app, fw_sample_fn fn, void *ctx);
void fw_band_app_set_link(fw_band_app_t *app, int up);
void fw_band_app_tick(fw_band_app_t *app);          /* 建议 �?ms 周期调用 */
void fw_band_app_rx_byte(fw_band_app_t *app, uint8_t b);

#endif /* FW_BAND_APP_H */
