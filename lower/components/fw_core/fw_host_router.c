#include "fw_host.h"
#include <string.h>

/* 下行帧路由器（通道 A）。上位机下行帧都是 15 字节定长（协议 §11 红线 2），
 * 所以这里用 15 字节滑窗即可，不需要变长组帧。 */

void fw_host_init(fw_host_router_t *h, const fw_host_hooks_t *hooks)
{
    if (h == NULL) return;
    memset(h, 0, sizeof(*h));
    if (hooks) h->hooks = *hooks;
}

/* 把缓冲头部 15 字节当作一帧处理，然后消费掉 */
static void handle_frame(fw_host_router_t *h, fw_band_app_t *app)
{
    uint8_t type = h->buf[3];

    if (fw_verify_frame(h->buf, FW_HOST_FRAME) != 0) {
        /* 校验失败：丢 1 字节重新同步，不往下传（避免 app 按错位数据校时） */
        memmove(h->buf, h->buf + 1, (size_t)(h->len - 1));
        h->len -= 1;
        ++h->stat_resync;
        return;
    }

    switch (type) {
    case FW_T_TIMESYNC:
    case FW_T_SYNCREQ:
        /* 真正干活的帧：整帧原样转交 fw_band_app */
        for (int i = 0; i < FW_HOST_FRAME; ++i) {
            if (app) fw_band_app_rx_byte(app, h->buf[i]);
        }
        ++h->stat_forwarded;
        break;

    case FW_T_CAPTURE:
        if (h->hooks.on_capture) h->hooks.on_capture(h->hooks.ctx, h->buf[10]);
        ++h->stat_captures;
        break;

    case FW_T_WIFI_CFG:
        if (h->hooks.on_wifi_cfg) h->hooks.on_wifi_cfg(h->hooks.ctx, h->buf + 10);
        ++h->stat_wifi_cfg;
        break;

    case FW_T_MOTOR:
        if (h->hooks.on_motor) {
            h->hooks.on_motor(h->hooks.ctx, h->buf[10], h->buf[11]);
        }
        ++h->stat_motor;
        break;

    default:
        break;
    }

    memmove(h->buf, h->buf + FW_HOST_FRAME, (size_t)(h->len - FW_HOST_FRAME));
    h->len -= FW_HOST_FRAME;
}

/* 把缓冲对齐到帧头。返回 1 = 已对齐（len>=2 且 AA 55 正确），0 = 还在找 */
static int sync_header(fw_host_router_t *h)
{
    for (;;) {
        if (h->len == 0) return 0;
        if (h->buf[0] != FW_HEAD0) {
            /* 丢到下一个可能的 0xAA */
            int i = 1;
            while (i < h->len && h->buf[i] != FW_HEAD0) ++i;
            memmove(h->buf, h->buf + i, (size_t)(h->len - i));
            h->len -= i;
            continue;
        }
        if (h->len == 1) return 0;                 /* 只有 0xAA，等下一字节 */
        if (h->buf[1] != FW_HEAD1) {
            /* 0xAA 后面不是 0x55：丢掉这个 0xAA，但它自己可能就是新帧头 */
            memmove(h->buf, h->buf + 1, (size_t)(h->len - 1));
            h->len -= 1;
            continue;
        }
        return 1;
    }
}

void fw_host_feed(fw_host_router_t *h, const uint8_t *data, int len, fw_band_app_t *app)
{
    int i = 0;

    if (h == NULL || data == NULL || len <= 0) return;

    while (i < len) {
        if (h->len == (int)sizeof(h->buf)) {
            /* 理论上不会满（每次都及时消费），保险起见丢最旧 1 字节 */
            memmove(h->buf, h->buf + 1, (size_t)(h->len - 1));
            h->len -= 1;
        }
        h->buf[h->len++] = data[i++];

        /* 尽可能多地摘帧 */
        while (h->len >= FW_HOST_FRAME) {
            if (!sync_header(h)) break;
            if (h->len < FW_HOST_FRAME) break;

            /* 只认"设备类型=上位机 且 长度字段=4"的定长下行帧；
             * 其它内容（噪声、别的帧）逐字节丢掉 */
            if (h->buf[2] != FW_DEV_HOST ||
                fw_get_u16(h->buf + 8) != FW_HOST_PAYLOAD) {
                memmove(h->buf, h->buf + 1, (size_t)(h->len - 1));
                h->len -= 1;
                continue;
            }
            handle_frame(h, app);
        }
    }
}
