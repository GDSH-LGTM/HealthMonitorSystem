#include "fw_image_tx.h"

#include <stddef.h>   /* NULL */

/* 图像发送（设备侧）：单槽、best-effort，见 fw_image_tx.h */

int fw_image_tx_build_start(uint8_t *buf, uint8_t image_id,
                            uint32_t total_bytes, uint32_t ts)
{
    return fw_build_image_start(buf, image_id, total_bytes,
                                fw_image_fragment_count(total_bytes), ts);
}

fw_image_tx_result_t fw_image_tx_send(const uint8_t *jpeg, uint32_t len,
                                      uint8_t image_id, uint32_t ts,
                                      fw_image_send_fn send, void *ctx)
{
    uint8_t  frame[FW_IMG_FRAG_FRAME_MAX];
    uint16_t count;
    uint16_t seq;
    int      n;

    if (send == NULL) return FW_IMAGE_TX_ERR_NO_SEND;
    if (jpeg == NULL || len == 0) return FW_IMAGE_TX_ERR_NO_DATA;
    if (image_id < FW_IMG_ID_MIN || image_id > FW_IMG_ID_MAX) {
        return FW_IMAGE_TX_ERR_BAD_ID;
    }

    count = fw_image_fragment_count(len);
    if (count == 0) return FW_IMAGE_TX_ERR_NO_DATA;

    /* 0x06 图像开始 */
    n = fw_build_image_start(frame, image_id, len, count, ts);
    if (!send(ctx, frame, (uint32_t)n)) return FW_IMAGE_TX_ERR_SEND_FAILED;

    /* 0x07 图像分片：1024 字节定长，末片可短 */
    for (seq = 0; seq < count; ++seq) {
        uint32_t off = (uint32_t)seq * FW_IMG_FRAG_DATA_MAX;
        uint32_t left = len - off;
        uint16_t chunk = (uint16_t)((left > FW_IMG_FRAG_DATA_MAX)
                                    ? FW_IMG_FRAG_DATA_MAX : left);

        n = fw_build_image_fragment(frame, image_id, seq,
                                    jpeg + off, chunk, ts);
        if (n < 0) return FW_IMAGE_TX_ERR_NO_DATA;
        if (!send(ctx, frame, (uint32_t)n)) return FW_IMAGE_TX_ERR_SEND_FAILED;
    }

    /* 0x08 图像结束：本版 CRC16 填 0，接收方不校验 */
    n = fw_build_image_end(frame, image_id, 0x0000, ts);
    if (!send(ctx, frame, (uint32_t)n)) return FW_IMAGE_TX_ERR_SEND_FAILED;

    return FW_IMAGE_TX_OK;
}
