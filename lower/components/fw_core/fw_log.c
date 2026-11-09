#include "fw_log.h"
#include <string.h>

#define FW_LOG_SLOT_SIZE 28
#define FW_LOG_SEQ_ERASED 0xFFFFFFFFu

static uint32_t slot_addr(const fw_log_t *log, uint32_t idx)
{
    if (log->slots_per_sector == 0)
        return idx * FW_LOG_SLOT_SIZE;
    return (idx / log->slots_per_sector) * log->st.sector_size
         + (idx % log->slots_per_sector) * FW_LOG_SLOT_SIZE;
}

static int slot_read(const fw_log_t *log, uint32_t idx, uint32_t *seq, uint8_t *frame24)
{
    uint8_t raw[FW_LOG_SLOT_SIZE];
    if (log->st.read(log->st.ctx, slot_addr(log, idx), raw, FW_LOG_SLOT_SIZE) != 0)
        return -1;
    if (seq) {
        *seq = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
             | ((uint32_t)raw[2] << 8)  | (uint32_t)raw[3];
    }
    if (frame24)
        memcpy(frame24, raw + 4, FW_VITAL_FRAME);
    return 0;
}

int fw_log_init(fw_log_t *log, const fw_storage_t *st)
{
    uint32_t i;
    uint32_t best_seq = 0, best_idx = 0;
    uint32_t found = 0;
    uint32_t count, idx, seq;

    memset(log, 0, sizeof(*log));
    log->st = *st;

    if (st->sector_size > 0) {
        log->slots_per_sector = st->sector_size / FW_LOG_SLOT_SIZE;
        log->slot_count = (st->size_bytes / st->sector_size) * log->slots_per_sector;
    } else {
        log->slots_per_sector = 0;
        log->slot_count = st->size_bytes / FW_LOG_SLOT_SIZE;
    }
    if (log->slot_count == 0)
        return -1;

    /* 1) 扫描找最新帧 */
    for (i = 0; i < log->slot_count; ++i) {
        uint32_t s;
        uint8_t frame[FW_VITAL_FRAME];
        if (slot_read(log, i, &s, frame) != 0) continue;
        if (s == FW_LOG_SEQ_ERASED) continue;
        if (!fw_slot_is_vital(frame)) continue;
        if (!found || s > best_seq) { best_seq = s; best_idx = i; found = 1; }
    }
    if (!found) { log->total = 0; log->oldest = 0; return 0; }

    /* 2) 从最新帧向旧回走：序号必须逐个 -1 */
    count = 1;
    idx = best_idx;
    seq = best_seq;
    while (count < log->slot_count) {
        const uint32_t pidx = (idx + log->slot_count - 1) % log->slot_count;
        uint32_t pseq;
        uint8_t frame[FW_VITAL_FRAME];
        if (slot_read(log, pidx, &pseq, frame) != 0) break;
        if (pseq == FW_LOG_SEQ_ERASED || pseq + 1 != seq) break;
        if (!fw_slot_is_vital(frame)) break;
        idx = pidx;
        seq = pseq;
        ++count;
    }
    log->total = best_seq + 1;
    log->oldest = log->total - count;
    return 0;
}

int fw_log_append(fw_log_t *log, const uint8_t *frame24)
{
    uint32_t idx, addr;
    uint8_t seqbuf[4];

    idx = log->total % log->slot_count;

    /* 环形覆盖：满则丢最旧一帧*/
    if (log->total - log->oldest >= log->slot_count)
        log->oldest = log->total - log->slot_count + 1;

    /* 进入新扇区：先整扇区擦除（其中保存的正是窗口内最旧的若干帧） */
    if (log->slots_per_sector > 0 && (idx % log->slots_per_sector) == 0) {
        const uint32_t sector = idx / log->slots_per_sector;
        uint32_t dropped = 0, k;
        if (log->st.erase_sector(log->st.ctx, sector) != 0)
            return -1;
        for (k = 0; k < log->slots_per_sector; ++k) {
            const uint32_t j = idx + k;
            int64_t fidx = -1;
            if (j >= log->slot_count) break;
            if (j < log->total)
                fidx = (int64_t)j;
            else if ((int64_t)j - (int64_t)log->slot_count >= (int64_t)log->oldest)
                fidx = (int64_t)j - (int64_t)log->slot_count;
            if (fidx >= (int64_t)log->oldest)
                ++dropped;
        }
        log->oldest += dropped;
    }

    /* 先写序号、再写帧体*/
    seqbuf[0] = (uint8_t)(log->total >> 24);
    seqbuf[1] = (uint8_t)(log->total >> 16);
    seqbuf[2] = (uint8_t)(log->total >> 8);
    seqbuf[3] = (uint8_t)(log->total);
    addr = slot_addr(log, idx);
    if (log->st.program(log->st.ctx, addr, seqbuf, 4) != 0) return -1;
    if (log->st.program(log->st.ctx, addr + 4, frame24, FW_VITAL_FRAME) != 0) return -1;
    ++log->total;
    return 0;
}

uint32_t fw_log_count(const fw_log_t *log)    { return log->total - log->oldest; }
uint32_t fw_log_capacity(const fw_log_t *log) { return log->slot_count; }
uint32_t fw_log_total(const fw_log_t *log)    { return log->total; }

int fw_log_get_seq(const fw_log_t *log, uint32_t seq, uint8_t *frame24)
{
    uint32_t idx, got;
    if (seq < log->oldest || seq >= log->total)
        return -1;
    idx = seq % log->slot_count;
    if (slot_read(log, idx, &got, frame24) != 0)
        return -1;
    if (got != seq)                 /* 书签与介质不一致：宁缺勿错 */
        return -1;
    if (!fw_slot_is_vital(frame24))
        return -1;
    return 0;
}

int fw_log_get(const fw_log_t *log, uint32_t i, uint8_t *frame24)
{
    if (i >= fw_log_count(log))
        return -1;
    return fw_log_get_seq(log, log->oldest + i, frame24);
}

int fw_log_find_since(const fw_log_t *log, uint32_t since_ts,
                      uint32_t *first_seq, uint32_t *count)
{
    uint32_t n = fw_log_count(log);
    uint32_t lo, hi;
    if (n == 0) return 0;
    if (since_ts == 0) {                     /* 0 = 请求全部 */
        if (first_seq) *first_seq = log->oldest;
        if (count)     *count = n;
        return 1;
    }
    /* 帧按时间升序 →二分找第一个ts > since（容量大时不能用线性扫描） */
    lo = 0;
    hi = n;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        uint8_t frame[FW_VITAL_FRAME];
        if (fw_log_get(log, mid, frame) != 0) return 0;
        if (fw_vital_ts(frame) > since_ts) hi = mid;
        else                               lo = mid + 1;
    }
    if (lo >= n) return 0;
    if (first_seq) *first_seq = log->oldest + lo;
    if (count)     *count = n - lo;
    return 1;
}
