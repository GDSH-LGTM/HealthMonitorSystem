#ifndef FW_HOST_H
#define FW_HOST_H

#include <stdint.h>
#include "fw_proto.h"
#include "fw_band_app.h"

/* 上位机下行帧路由器（通道 A）
 *
 * 0x03 校时 / 0x05 补传请求 由 fw_band_app 自己处理；
 * 0x09 WiFi 配置 / 0x0B 拍照 / 0x0A 震动 是"板级动作"，app 层不该管，
 * 所以这里先嗅探一遍再转交。
 *
 * 为什么不让 fw_band_app 自己解析：那样要改它的签名或加回调，
 * 会把纯 C 的协议核心和板级动作耦在一起，也会打断上位机的 firmware_smoke。 */

typedef void (*fw_host_capture_fn)(void *ctx, uint8_t image_id);
typedef void (*fw_host_wifi_fn)(void *ctx, const uint8_t ip[4]);
typedef void (*fw_host_motor_fn)(void *ctx, uint8_t warn_type, uint8_t motor_mode);

typedef struct {
    fw_host_capture_fn on_capture;   /* 0x0B */
    fw_host_wifi_fn    on_wifi_cfg;  /* 0x09 */
    fw_host_motor_fn   on_motor;     /* 0x0A（本版不实现，可为 NULL） */
    void              *ctx;
} fw_host_hooks_t;

/* 注意：类型名不能叫 fw_host_t —— fw_proto.h 里已用该名字表示
 * 0x03/0x05 的载荷结构体。这里是"下行帧路由器"，故用 fw_host_router_t。 */
typedef struct {
    uint8_t  buf[FW_HOST_FRAME];     /* 滑窗：下行帧都是 15 字节定长 */
    int      len;
    fw_host_hooks_t hooks;

    uint32_t stat_captures;          /* 收到的 0x0B 帧数 */
    uint32_t stat_wifi_cfg;          /* 收到的 0x09 帧数 */
    uint32_t stat_motor;             /* 收到的 0x0A 帧数 */
    uint32_t stat_resync;            /* 因校验失败丢弃的帧数 */
    uint32_t stat_forwarded;         /* 转交 fw_band_app 的帧数（0x03/0x05） */
} fw_host_router_t;

void fw_host_init(fw_host_router_t *h, const fw_host_hooks_t *hooks);

/* 喂入通道 A 收到的字节：0x09/0x0B/0x0A 就地处理，0x03/0x05 转交 app */
void fw_host_feed(fw_host_router_t *h, const uint8_t *data, int len, fw_band_app_t *app);

#endif /* FW_HOST_H */
