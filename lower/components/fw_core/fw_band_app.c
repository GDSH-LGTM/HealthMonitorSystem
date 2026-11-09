#include "fw_band_app.h"
#include <string.h>

#define FW_SAMPLE_PERIOD_MS 1000u
#define FW_REPLAY_PERIOD_MS 20u
#define FW_SECS_PER_DAY     86400u

static void send_bytes(fw_band_app_t *app, const uint8_t *buf, int len)
{
    if (app->uart.send)
        app->uart.send(app->uart.ctx, buf, (uint32_t)len);
}

static void sample_tick(fw_band_app_t *app)
{
    fw_vital_t v;
    uint8_t frame[FW_VITAL_FRAME];
    uint32_t day;

    memset(&v, 0, sizeof(v));
    if (app->sample_fn)
        app->sample_fn(app->sample_ctx, &v);
    v.ts = app->clk.epoch_now ? app->clk.epoch_now(app->clk.ctx) : 0;

    if (v.ts != 0) {
        day = v.ts / FW_SECS_PER_DAY;
        if (day != app->last_day) {          /* 跨零点：清零当日累计 */
            app->calorie = 0;
            app->last_day = day;
        }
    }

    /* 占位热量公式（真机接运动传感器后替换）：心率>100 �?+3 kcal/s，否�?+1 kcal/s */
    app->calorie += (v.hr != FW_HR_INVALID && v.hr > 100u) ? 3u : 1u;
    v.cal = app->calorie;

    fw_build_vital(frame, &v);
    fw_log_append(&app->log, frame);          /* 无论链路状态都先进缓存 */
    ++app->stat_samples;

    if (app->link_up && !app->replaying) {    /* 实时发送（补传期间不发，防抢会话） */
        send_bytes(app, frame, FW_VITAL_FRAME);
        ++app->stat_live_sent;
    }
}

static void start_replay(fw_band_app_t *app, uint32_t since_ts)
{
    uint32_t first_seq = 0, count = 0;
    uint8_t buf[FW_BACKLOG_FRAME];

    if (!fw_log_find_since(&app->log, since_ts, &first_seq, &count))
        return;   /* since 之后无帧：静默，不发 0x04（协议约定） */
    if (count > 0xFFFFu)
        count = 0xFFFFu;                      /* count 仅用�?UI 展示，超限截�?*/
    fw_build_backlog(buf, 1, (uint16_t)count, app->clk.epoch_now(app->clk.ctx));
    send_bytes(app, buf, FW_BACKLOG_FRAME);
    app->replaying      = 1;
    app->replay_seq     = first_seq;
    app->last_replay_ms = app->clk.millis(app->clk.ctx);
    app->stat_replayed  = 0;
}

static void replay_tick(fw_band_app_t *app, uint32_t now_ms)
{
    uint8_t frame[FW_VITAL_FRAME];

    if ((uint32_t)(now_ms - app->last_replay_ms) < FW_REPLAY_PERIOD_MS)
        return;
    app->last_replay_ms = now_ms;

    /* 边补边追：补传期间新采样的帧也在日志尾部，一起补出去 */
    if (app->replay_seq < fw_log_total(&app->log)) {
        if (fw_log_get_seq(&app->log, app->replay_seq, frame) == 0) {
            send_bytes(app, frame, FW_VITAL_FRAME);
            ++app->stat_replayed;
        }
        ++app->replay_seq;                    /* 坏帧直接跳过，不卡死 */
        return;
    }

    {   /* 追上日志尾部�?x04 结束，恢复实�?*/
        uint8_t buf[FW_BACKLOG_FRAME];
        fw_build_backlog(buf, 0, 0, app->clk.epoch_now(app->clk.ctx));
        send_bytes(app, buf, FW_BACKLOG_FRAME);
    }
    app->replaying = 0;
}

static void handle_host(fw_band_app_t *app, const fw_host_t *h)
{
    app->link_up = 1;                         /* 收到主机�?�?链路已�?*/
    if (h->type == FW_T_TIMESYNC) {
        if (app->clk.set_epoch)
            app->clk.set_epoch(app->clk.ctx, h->epoch);
        app->calorie  = 0;                    /* 校时同时清零当日累计（协议语义） */
        app->last_day = h->epoch / FW_SECS_PER_DAY;
    } else if (h->type == FW_T_SYNCREQ) {
        if (!app->replaying)                  /* 补传中收到新请求：忽�?*/
            start_replay(app, h->epoch);
    }
}

void fw_band_app_init(fw_band_app_t *app, const fw_storage_t *st,
                      const fw_uart_t *uart, const fw_clock_t *clk)
{
    memset(app, 0, sizeof(*app));
    fw_log_init(&app->log, st);
    app->uart = *uart;
    app->clk  = *clk;
    app->link_up = 1;                         /* 默认视为链路通（有线调试 / �?STATE 引脚�?*/
    {
        const uint32_t epoch = app->clk.epoch_now ? app->clk.epoch_now(app->clk.ctx) : 0;
        app->last_day = epoch / FW_SECS_PER_DAY;
        app->last_sample_ms = app->clk.millis ? app->clk.millis(app->clk.ctx) : 0;
    }
    app->last_replay_ms = app->last_sample_ms;
}

void fw_band_app_set_sample_source(fw_band_app_t *app, fw_sample_fn fn, void *ctx)
{
    app->sample_fn  = fn;
    app->sample_ctx = ctx;
}

void fw_band_app_set_link(fw_band_app_t *app, int up)
{
    app->link_up = up ? 1 : 0;
}

void fw_band_app_tick(fw_band_app_t *app)
{
    const uint32_t now = app->clk.millis ? app->clk.millis(app->clk.ctx) : 0;
    if ((uint32_t)(now - app->last_sample_ms) >= FW_SAMPLE_PERIOD_MS) {
        app->last_sample_ms = now;            /* 节拍以实�?tick 为准，不补采 */
        sample_tick(app);
    }
    if (app->replaying)
        replay_tick(app, now);
}

void fw_band_app_rx_byte(fw_band_app_t *app, uint8_t b)
{
    if (app->rxlen == 0 && b != FW_HEAD0)
        return;                               /* 等帧头首字节 */
    app->rxbuf[app->rxlen++] = b;
    if (app->rxlen < FW_HOST_FRAME)
        return;
    {
        fw_host_t h;
        if (fw_parse_host(app->rxbuf, FW_HOST_FRAME, &h) == 0) {
            app->rxlen = 0;
            handle_host(app, &h);
        } else {
            memmove(app->rxbuf, app->rxbuf + 1, FW_HOST_FRAME - 1);  /* 滑窗重同�?*/
            app->rxlen = FW_HOST_FRAME - 1;
        }
    }
}
