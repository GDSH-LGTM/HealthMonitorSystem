#include "fw_image_rx.h"
#include <string.h>

/* 图像分片重组实现（见 fw_image_rx.h 的规则说明） */

/* 位图操作 */
static void seen_set(fw_image_rx_t *rx, uint16_t seq)
{
    if (seq / 8u < rx->seq_seen_cap) {
        rx->seq_seen[seq / 8u] |= (uint8_t)(1u << (seq % 8u));
    }
}

static int seen_test(const fw_image_rx_t *rx, uint16_t seq)
{
    if (seq / 8u >= rx->seq_seen_cap) return 0;
    return (rx->seq_seen[seq / 8u] >> (seq % 8u)) & 0x01u;
}

static uint32_t now_of(const fw_image_rx_t *rx)
{
    return rx->now_ms ? rx->now_ms(rx->now_ctx) : 0u;
}

/* 时间差（处理 32 位毫秒回绕） */
static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return (uint32_t)(now - then);
}

void fw_image_rx_init(fw_image_rx_t *rx, uint8_t *buf, uint32_t buf_cap,
                      uint8_t *seq_seen, uint32_t seq_seen_cap,
                      fw_now_ms_fn now_ms, void *now_ctx)
{
    memset(rx, 0, sizeof(*rx));
    rx->buf          = buf;
    rx->buf_cap      = buf_cap;
    rx->seq_seen     = seq_seen;
    rx->seq_seen_cap = seq_seen_cap;
    rx->now_ms       = now_ms;
    rx->now_ctx      = now_ctx;
    rx->timeout_ms   = FW_IMAGE_TIMEOUT_MS_DEFAULT;
    rx->image_id     = -1;
    rx->state        = FW_IMAGE_STATE_IDLE;
}

void fw_image_rx_set_timeout_ms(fw_image_rx_t *rx, uint32_t ms)
{
    if (ms > 0) rx->timeout_ms = ms;
}

void fw_image_rx_reset(fw_image_rx_t *rx)
{
    rx->image_id       = -1;
    rx->total_bytes    = 0;
    rx->received_bytes = 0;
    rx->frag_declared  = 0;
    rx->frag_expected  = 0;
    rx->frag_received  = 0;
    rx->capture_ts     = 0;
    rx->crc16          = 0;
    rx->buf_len        = 0;
    rx->last_frag_ms   = 0;
    if (rx->seq_seen && rx->seq_seen_cap) {
        memset(rx->seq_seen, 0, rx->seq_seen_cap);
    }
    rx->state = FW_IMAGE_STATE_IDLE;
}

fw_image_result_t fw_image_rx_start(fw_image_rx_t *rx, uint8_t image_id,
                                    uint32_t total_bytes, uint16_t fragment_count,
                                    uint32_t capture_ts)
{
    uint32_t expected;
    uint32_t need_bytes;

    /* 新图抢占：重组中收到新的 0x06 → 丢弃旧图、建新槽（单槽语义） */
    if (rx->state == FW_IMAGE_STATE_RECEIVING) {
        rx->last_discard = FW_IMAGE_DISCARD_PREEMPTED;
    }
    /* 无条件释放槽位：IDLE/RECEIVING/COMPLETE/ERROR 都可被新图覆盖 */
    fw_image_rx_reset(rx);

    /* 头字段非法（损坏帧）：不建槽 */
    if (total_bytes == 0 || fragment_count == 0 || total_bytes > FW_IMAGE_BYTES_BUDGET) {
        rx->state        = FW_IMAGE_STATE_ERROR;
        rx->last_discard = FW_IMAGE_DISCARD_BAD_HEADER;
        return FW_IMAGE_ERR_BAD_HEADER;
    }

    expected = ((total_bytes + FW_IMG_FRAG_DATA_MAX - 1) / FW_IMG_FRAG_DATA_MAX);
    if (expected != (uint32_t)fragment_count) {
        /* 声明的分片数与总字节对不上：发送端有问题，不收 */
        rx->state        = FW_IMAGE_STATE_ERROR;
        rx->last_discard = FW_IMAGE_DISCARD_BAD_HEADER;
        return FW_IMAGE_ERR_BAD_HEADER;
    }

    /* 缓冲区是否够放整图 */
    need_bytes = total_bytes;
    if (need_bytes > rx->buf_cap) {
        rx->state        = FW_IMAGE_STATE_ERROR;
        rx->last_discard = FW_IMAGE_DISCARD_BAD_HEADER;
        return FW_IMAGE_ERR_NO_BUFFER;
    }
    /* 去重位图是否够放（ceil(fragment_count / 8)） */
    if (((uint32_t)fragment_count + 7u) / 8u > rx->seq_seen_cap) {
        rx->state        = FW_IMAGE_STATE_ERROR;
        rx->last_discard = FW_IMAGE_DISCARD_BAD_HEADER;
        return FW_IMAGE_ERR_NO_BUFFER;
    }

    rx->image_id      = (int)image_id;
    rx->total_bytes   = total_bytes;
    rx->frag_declared = fragment_count;
    rx->frag_expected = (uint16_t)expected;
    rx->capture_ts    = capture_ts;
    rx->last_frag_ms  = now_of(rx);   /* 建槽即开始计时（首片也受超时保护） */
    rx->state         = FW_IMAGE_STATE_RECEIVING;
    rx->last_discard  = FW_IMAGE_DISCARD_NONE;
    return FW_IMAGE_OK;
}

fw_image_result_t fw_image_rx_fragment(fw_image_rx_t *rx, uint8_t image_id,
                                       uint16_t seq, const uint8_t *data, uint16_t data_len)
{
    uint32_t offset;
    int      fail_id;
    uint16_t fail_declared;

    /* 未建槽 / 复位后迟到的分片：图 ID 不匹配即忽略 */
    if (rx->state != FW_IMAGE_STATE_RECEIVING) {
        return FW_IMAGE_ERR_BUSY;
    }
    if ((int)image_id != rx->image_id) {
        return FW_IMAGE_ERR_ID_MISMATCH;
    }
    if (seq >= rx->frag_declared) {
        /* 序号越界：丢弃该片，不整块丢弃 */
        return FW_IMAGE_ERR_SEQ_RANGE;
    }
    if (data_len == 0 || data_len > FW_IMG_FRAG_DATA_MAX) {
        return FW_IMAGE_ERR_TOO_BIG;
    }
    if (seen_test(rx, seq)) {
        /* 重复片：忽略（不覆盖、不计数），避免破坏"分片数/字节和"校验 */
        return FW_IMAGE_ERR_DUPLICATE;
    }

    offset = (uint32_t)seq * FW_IMG_FRAG_DATA_MAX;

    /* 越界保护：不写爆缓冲；这两种情况整张丢弃并释放槽位 */
    if (offset + data_len > rx->buf_cap || offset + data_len > rx->total_bytes) {
        fw_image_result_t r = (offset + data_len > rx->buf_cap)
                            ? FW_IMAGE_ERR_NO_BUFFER : FW_IMAGE_ERR_BAD_HEADER;
        fail_id       = rx->image_id;
        fail_declared = rx->frag_declared;
        fw_image_rx_reset(rx);
        rx->image_id      = fail_id;        /* 保留标识供日志 */
        rx->frag_declared = fail_declared;
        rx->state         = FW_IMAGE_STATE_ERROR;
        rx->last_discard  = FW_IMAGE_DISCARD_BAD_HEADER;
        return r;
    }

    memcpy(rx->buf + offset, data, data_len);
    seen_set(rx, seq);
    rx->frag_received++;
    rx->received_bytes += data_len;
    if (offset + data_len > rx->buf_len) {
        rx->buf_len = offset + data_len;
    }
    rx->last_frag_ms = now_of(rx);   /* "连续无新分片"：每个有效分片重置计时 */
    return FW_IMAGE_OK;
}

fw_image_result_t fw_image_rx_end(fw_image_rx_t *rx, uint8_t image_id, uint16_t crc16)
{
    uint8_t  fail_id;
    uint32_t fail_total;
    uint16_t fail_declared;
    uint16_t fail_received;
    uint32_t fail_bytes;

    if (rx->state != FW_IMAGE_STATE_RECEIVING) {
        return FW_IMAGE_ERR_BUSY;
    }
    if ((int)image_id != rx->image_id) {
        return FW_IMAGE_ERR_ID_MISMATCH;
    }

    /* 完整性前两项（第三项"每帧校验和"在解析层已完成） */
    if (rx->frag_received != rx->frag_declared ||
        rx->received_bytes != rx->total_bytes) {
        fail_id       = (uint8_t)rx->image_id;
        fail_total    = rx->total_bytes;
        fail_declared = rx->frag_declared;
        fail_received = rx->frag_received;
        fail_bytes    = rx->received_bytes;
        fw_image_rx_reset(rx);
        rx->image_id      = fail_id;
        rx->total_bytes   = fail_total;
        rx->frag_declared = fail_declared;
        rx->frag_received = fail_received;
        rx->received_bytes = fail_bytes;
        rx->state         = FW_IMAGE_STATE_ERROR;
        rx->last_discard  = FW_IMAGE_DISCARD_INCOMPLETE;
        return FW_IMAGE_ERR_INCOMPLETE;
    }

    rx->crc16   = crc16;   /* 本版不校验 */
    rx->buf_len = rx->total_bytes;
    rx->state   = FW_IMAGE_STATE_COMPLETE;   /* 数据留在缓冲，等调用者取走后再 reset */
    return FW_IMAGE_OK;
}

int fw_image_rx_poll_timeout(fw_image_rx_t *rx)
{
    int fail_id;

    if (rx->state != FW_IMAGE_STATE_RECEIVING) return 0;
    if (elapsed_ms(now_of(rx), rx->last_frag_ms) < rx->timeout_ms) return 0;

    fail_id = rx->image_id;
    fw_image_rx_reset(rx);
    rx->image_id     = fail_id;                /* 保留标识供日志 */
    rx->state        = FW_IMAGE_STATE_ERROR;   /* 复位后保留"超时"这个结论 */
    rx->last_discard = FW_IMAGE_DISCARD_TIMEOUT;
    return 1;
}

fw_image_state_t fw_image_rx_state(const fw_image_rx_t *rx)
{
    return rx->state;
}

int fw_image_rx_is_ready(const fw_image_rx_t *rx)
{
    return (rx->state == FW_IMAGE_STATE_COMPLETE) ? 1 : 0;
}

const uint8_t *fw_image_rx_data(const fw_image_rx_t *rx)
{
    return rx->buf;
}

uint32_t fw_image_rx_size(const fw_image_rx_t *rx)
{
    return (rx->state == FW_IMAGE_STATE_COMPLETE) ? rx->total_bytes : 0u;
}

fw_image_discard_t fw_image_rx_last_discard(const fw_image_rx_t *rx)
{
    return rx->last_discard;
}
