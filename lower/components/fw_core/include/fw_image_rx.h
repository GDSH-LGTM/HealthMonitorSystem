#ifndef FW_IMAGE_RX_H
#define FW_IMAGE_RX_H

#include <stdint.h>
#include "fw_proto.h"

/* 图像分片重组（设备侧镜像实现，与上位机 communication/imageassembler.cpp 同一套规则）
 *
 * 单槽状态机：收 0x06 建槽 → 收 0x07 按序号填片 → 收 0x08 收尾。
 * 三项完整性校验全过才算成功：
 *   1) 实收分片数 == 0x06 声明的分片数
 *   2) 各分片字节和 == 0x06 声明的总字节
 *   3) 每一帧的帧校验和通过（由 fw_rx_t / fw_verify_frame 在解析层完成）
 *
 * 单槽五场景（与 PC 侧逐一对齐）：
 *   正常 / 乱序（按序号落位）/ 丢片→超时丢弃 / 重复片（忽略、不计数）/ 新图抢占（丢旧建新）
 * 复位后迟到的分片按图 ID 忽略。
 *
 * 缓冲区由调用者提供，本模块不分配内存（嵌入式无堆）：
 *   fw_image_rx_init(&rx, buf, buf_cap, now_ms_cb, ctx);
 *   buf_cap 必须 >= 预期最大整图字节数（QVGA JPEG 通常 8~15KB）。
 * 本版不校验 CRC16（0x08 载荷为保留字段）。 */

/* 单张图的字节数防御上限（0x06 头被损坏时不至于按声明值越界） */
#ifndef FW_IMAGE_BYTES_BUDGET
#define FW_IMAGE_BYTES_BUDGET (1024u * 1024u)
#endif

/* 分片超时默认值（连续无新分片），与协议 §11 及上位机一致 */
#ifndef FW_IMAGE_TIMEOUT_MS_DEFAULT
#define FW_IMAGE_TIMEOUT_MS_DEFAULT 5000u
#endif

typedef enum {
    FW_IMAGE_STATE_IDLE = 0,   /* 无槽 */
    FW_IMAGE_STATE_RECEIVING,
    FW_IMAGE_STATE_COMPLETE,   /* 校验通过，等调用者取走 */
    FW_IMAGE_STATE_ERROR       /* 最近一次失败，reason 有效 */
} fw_image_state_t;

typedef enum {
    FW_IMAGE_OK = 0,
    FW_IMAGE_ERR_BUSY,          /* 槽外收到分片/结束帧 */
    FW_IMAGE_ERR_BAD_HEADER,    /* 0x06 字段非法（0 / 超预算 / 分片数不符） */
    FW_IMAGE_ERR_ID_MISMATCH,   /* 图 ID 不匹配（复位后迟到分片） */
    FW_IMAGE_ERR_SEQ_RANGE,     /* 分片序号越界 */
    FW_IMAGE_ERR_DUPLICATE,     /* 重复片，已忽略 */
    FW_IMAGE_ERR_TOO_BIG,       /* 单帧数据超过 1024 */
    FW_IMAGE_ERR_INCOMPLETE,    /* 收尾时完整性校验未过 */
    FW_IMAGE_ERR_NO_BUFFER      /* 调用者缓冲区不足 */
} fw_image_result_t;

typedef enum {
    FW_IMAGE_DISCARD_NONE = 0,
    FW_IMAGE_DISCARD_TIMEOUT,
    FW_IMAGE_DISCARD_PREEMPTED,
    FW_IMAGE_DISCARD_INCOMPLETE,
    FW_IMAGE_DISCARD_BAD_HEADER
} fw_image_discard_t;

typedef uint32_t (*fw_now_ms_fn)(void *ctx);

typedef struct {
    /* 槽位元数据（来自 0x06） */
    int      image_id;
    uint32_t total_bytes;      /* 0x06 声明 */
    uint32_t received_bytes;   /* 实收字节和 */
    uint16_t frag_declared;    /* 0x06 声明分片数 */
    uint16_t frag_expected;    /* ceil(total_bytes / 1024) —— 用于交叉校验 */
    uint16_t frag_received;    /* 实收分片数 */
    uint32_t capture_ts;       /* 0x06 帧头时间戳 = 采集时刻 */
    uint16_t crc16;            /* 0x08 带回，本版不校验 */

    uint8_t *buf;              /* 调用者提供的重组缓冲 */
    uint32_t buf_cap;
    uint32_t buf_len;          /* 已填充字节数 */

    uint8_t  *seq_seen;        /* 去重位图（frag_declared 位） */
    uint32_t seq_seen_cap;

    uint32_t timeout_ms;
    uint32_t last_frag_ms;

    fw_now_ms_fn now_ms;
    void        *now_ctx;

    fw_image_state_t   state;
    fw_image_discard_t last_discard;
} fw_image_rx_t;

/* 初始化；buf/seq_seen 由调用者提供且生命周期需覆盖整个使用期 */
void fw_image_rx_init(fw_image_rx_t *rx, uint8_t *buf, uint32_t buf_cap,
                      uint8_t *seq_seen, uint32_t seq_seen_cap,
                      fw_now_ms_fn now_ms, void *now_ctx);

void fw_image_rx_set_timeout_ms(fw_image_rx_t *rx, uint32_t ms);
void fw_image_rx_reset(fw_image_rx_t *rx);          /* 静默复位 */

fw_image_result_t fw_image_rx_start(fw_image_rx_t *rx, uint8_t image_id,
                                    uint32_t total_bytes, uint16_t fragment_count,
                                    uint32_t capture_ts);
fw_image_result_t fw_image_rx_fragment(fw_image_rx_t *rx, uint8_t image_id,
                                       uint16_t seq, const uint8_t *data, uint16_t data_len);
fw_image_result_t fw_image_rx_end(fw_image_rx_t *rx, uint8_t image_id, uint16_t crc16);

/* 需周期性调用（或收到分片前调用）；超时则丢弃整张并记录 reason */
int fw_image_rx_poll_timeout(fw_image_rx_t *rx);

fw_image_state_t   fw_image_rx_state(const fw_image_rx_t *rx);
int                fw_image_rx_is_ready(const fw_image_rx_t *rx);
const uint8_t     *fw_image_rx_data(const fw_image_rx_t *rx);
uint32_t           fw_image_rx_size(const fw_image_rx_t *rx);
fw_image_discard_t fw_image_rx_last_discard(const fw_image_rx_t *rx);

#endif /* FW_IMAGE_RX_H */
