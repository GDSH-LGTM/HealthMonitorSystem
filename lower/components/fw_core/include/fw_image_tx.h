#ifndef FW_IMAGE_TX_H
#define FW_IMAGE_TX_H

#include <stdint.h>
#include "fw_proto.h"

/* 图像发送（设备侧，通道 B）
 *
 * 流程（协议 §8.2）：
 *   PC 发 0x0B(图ID) → 设备采集 JPEG → 0x06(图ID/总字节/分片数) →
 *   0x07 × N（1024 字节定长切片，末片可短）→ 0x08(图ID/CRC16=0)
 *
 * best-effort：无 ACK、无重传；发送中断即整张丢弃（协议 §8.1）
 * 单槽：同一时刻只传一张图
 *
 * 本模块只做组帧与切片，实际发送靠调用者注入的回调——这样它保持纯 C，
 * 既能上板（回调里走 WiFi TCP），也能在 PC 上做自测。 */

/* 发送回调：返回 1 表示已送出，0 表示失败（失败则整个发送过程提前结束） */
typedef int (*fw_image_send_fn)(void *ctx, const uint8_t *data, uint32_t len);

typedef enum {
    FW_IMAGE_TX_OK = 0,
    FW_IMAGE_TX_ERR_NO_DATA,     /* 传入数据为空 */
    FW_IMAGE_TX_ERR_BAD_ID,      /* 图 ID 不在 1~0x7F */
    FW_IMAGE_TX_ERR_NO_SEND,     /* 未提供发送回调 */
    FW_IMAGE_TX_ERR_SEND_FAILED  /* 回调返回失败（通常是通道断开） */
} fw_image_tx_result_t;

/* 把一张图按协议发送出去。返回 FW_IMAGE_TX_OK 表示三部分都送出。
 * ts 为采集时刻（epoch 秒，未校时传 0），三帧共用同一个 ts（协议 §2） */
fw_image_tx_result_t fw_image_tx_send(const uint8_t *jpeg, uint32_t len,
                                      uint8_t image_id, uint32_t ts,
                                      fw_image_send_fn send, void *ctx);

/* 只组 0x06 帧（供测试或分步发送用）。返回整帧长 */
int fw_image_tx_build_start(uint8_t *buf, uint8_t image_id,
                            uint32_t total_bytes, uint32_t ts);

#endif /* FW_IMAGE_TX_H */
