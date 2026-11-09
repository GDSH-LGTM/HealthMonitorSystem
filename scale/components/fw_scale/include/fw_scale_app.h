#ifndef FW_SCALE_APP_H
#define FW_SCALE_APP_H

#include <stdint.h>
#include "hx711.h"

/* 体重秤应用：采样 → 稳定判定 → 出 0x02 体重帧
 *
 * 协议（protocol_spec1.2.md §3 / §4.2）：
 *   0x02 体重数据帧  秤→PC  载荷 2 字节 = 体重 ×100（单位 0.01kg），0xFFFF = 无效
 *   整帧 13 字节（10 头 + 2 载荷 + 1 校验）
 *   上报时机：**称重稳定后发 1 帧**；秤不缓存、不补传（§5）
 *
 * 标定值存 NVS，掉电不丢。首次未标定时重量恒为 0，
 * 需按 docs/CALIBRATION.md 走一遍去皮 + 砝码标定。 */

/* 采样窗口最大长度（先定义，下面结构体要用） */
#ifndef SCALE_WIN_MAX
#define SCALE_WIN_MAX 32
#endif

typedef struct {
    hx711_t  hx;

    /* 采样窗口（环形，用于稳定判定） */
    float    win[SCALE_WIN_MAX];
    int      win_count;
    int      win_pos;
    float    grams;           /* 最近一次滤波后的重量 */

    /* 稳定判定 */
    int      stable_hits;     /* 连续稳定次数 */
    int      stable;          /* 1 = 当前稳定 */
    int      reported;        /* 1 = 本次稳定已经上报过（避免重复发帧） */

    uint32_t ms;              /* 1ms 节拍（由 tick 累加） */
    uint32_t last_sample_ms;

    /* 统计 */
    uint32_t stat_samples;
    uint32_t stat_frames;     /* 已发出 0x02 帧数 */
    uint32_t stat_read_err;   /* 读 HX711 失败次数 */

    int      calibrated;      /* 1 = scale 有效（已标定） */
} fw_scale_app_t;

/* 初始化：GPIO、NVS 恢复标定值 */
int  fw_scale_app_init(fw_scale_app_t *a);

/* 1ms 节拍调用。返回 1 表示"本次产生了需要上报的稳定重量"（读 a->grams） */
int  fw_scale_app_tick(fw_scale_app_t *a);

/* 当前重量（克，仅用于显示/日志） */
float fw_scale_app_grams(const fw_scale_app_t *a);

/* 去皮：把当前读数记为零点并落 NVS。返回 0 成功 */
int  fw_scale_app_tare(fw_scale_app_t *a);

/* 标定：放上 known_grams 克砝码后调用，落 NVS。返回 0 成功 */
int  fw_scale_app_calibrate(fw_scale_app_t *a, float known_grams);

/* 1 = 已标定（读数可信） */
int  fw_scale_app_calibrated(const fw_scale_app_t *a);

/* 当前稳定窗口均值（克）——稳定时用均值比瞬时值更准 */
float fw_scale_app_stable_grams(const fw_scale_app_t *a);

#endif /* FW_SCALE_APP_H */
