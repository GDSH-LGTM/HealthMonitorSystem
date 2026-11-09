#include "scale_frame.h"

#include <stddef.h>   /* NULL */

/* 0x02 体重帧组装（协议 §4.2）
 *   载荷 2 字节 = 体重 ×100（单位 0.01kg），大端；0xFFFF = 无效
 *   整帧 13 字节 = 10 头 + 2 载荷 + 1 校验
 *
 * 例：65.43kg → 6543 → 帧内 0x19 0x8F
 * 上位机侧对应 protocolparser.cpp：
 *   quint16 w; s >> w;  data.weight = (w == 0xFFFF) ? -1.0f : w / 100.0f; */

/* 量程上限（克）：载荷是 16 位，0xFFFF 留作无效值，故最大 65534 × 10g = 655.34kg */
#define SCALE_FRAME_MAX_GRAMS 655340.0f

uint16_t scale_grams_to_payload(float grams)
{
    float units;

    if (grams < 0.0f || grams > SCALE_FRAME_MAX_GRAMS) {
        return FW_WEIGHT_INVALID;
    }
    /* 克 → 0.01kg：除以 10（1 单位 = 10g）*/
    units = grams / 10.0f;
    if (units > 65534.0f) {
        return FW_WEIGHT_INVALID;
    }
    return (uint16_t)(units + 0.5f);   /* 四舍五入 */
}

float scale_payload_to_kg(uint16_t payload)
{
    if (payload == FW_WEIGHT_INVALID) return -1.0f;
    return (float)payload / 100.0f;
}

int scale_build_weight_frame(uint8_t *buf, float grams, uint32_t ts, int calibrated)
{
    fw_weight_t w;

    if (buf == NULL) return -1;

    w.ts          = ts;
    w.weight_x100 = calibrated ? scale_grams_to_payload(grams) : FW_WEIGHT_INVALID;
    return fw_build_weight(buf, &w);
}
