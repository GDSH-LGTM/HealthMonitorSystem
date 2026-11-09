#ifndef SCALE_FRAME_H
#define SCALE_FRAME_H

#include <stdint.h>
#include "fw_proto.h"

/* 体重秤的 0x02 帧组装（纯逻辑，无平台依赖）
 *
 * 单独拆出来是为了能在 PC 上直接编译自测 —— 这样烧板之前就能证明
 * 秤组出来的帧与上位机 protocolparser.cpp 的解析口径一致。
 * 依赖硬件的部分（HX711 读取、稳定判定、NVS）在 fw_scale_app.c。 */

/* 克 → 协议载荷（单位 0.01kg，即 ×100）。
 * 无效（负值 / 超量程 / 超过 65534）返回 FW_WEIGHT_INVALID(0xFFFF) */
uint16_t scale_grams_to_payload(float grams);

/* 载荷 → 千克（供日志/显示；0xFFFF 返回 -1.0f 表示无效） */
float scale_payload_to_kg(uint16_t payload);

/* 组 0x02 帧：载荷来自 grams。写满 13 字节返回 FW_WEIGHT_FRAME(13)。
 * calibrated=0 时载荷填 0xFFFF（未标定的读数不可信，按协议发无效值） */
int scale_build_weight_frame(uint8_t *buf, float grams, uint32_t ts, int calibrated);

#endif /* SCALE_FRAME_H */
