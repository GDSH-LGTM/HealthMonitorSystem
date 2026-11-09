/* 体重秤帧组装自测（纯 C，可在 PC 上直接编译运行，不需要 ESP-IDF、不需要硬件）
 *
 * 目的：烧板之前就证明秤组出的 0x02 帧与上位机 communication/protocolparser.cpp
 *       的解析口径一致 —— 上位机那段是：
 *         quint16 w; s >> w;
 *         data.weight = (w == 0xFFFF) ? -1.0f : w / 100.0f;
 *
 * 编译（MinGW）：
 *   gcc -std=c99 -Wall -Wextra -I ../lower/components/fw_core/include -I components/fw_scale/include \
 *       test/scale_selftest.c components/fw_scale/scale_frame.c \
 *       ../lower/components/fw_core/fw_proto.c -o test/scale_selftest.exe
 */

#include <stdio.h>
#include <string.h>
#include "fw_proto.h"
#include "scale_frame.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) {
        ++g_pass;
        printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        printf("[FAIL] %s\n", name);
    }
}

/* 模拟上位机解析：从 13 字节帧里取出体重（kg），无效返回 -1.0f */
static float parse_like_upper_computer(const uint8_t *frame, int len)
{
    uint16_t w;

    if (len != FW_WEIGHT_FRAME) return -2.0f;
    if (frame[0] != FW_HEAD0 || frame[1] != FW_HEAD1) return -3.0f;
    if (frame[2] != FW_DEV_SCALE) return -4.0f;
    if (frame[3] != FW_T_WEIGHT) return -5.0f;
    if (fw_get_u16(frame + 8) != FW_WEIGHT_PAYLOAD) return -6.0f;
    if (frame[len - 1] != fw_checksum(frame, len - 1)) return -7.0f;

    w = fw_get_u16(frame + 10);
    return (w == FW_WEIGHT_INVALID) ? -1.0f : (float)w / 100.0f;
}

static void test_payload_conversion(void)
{
    printf("\n---- A 段：克 → 协议载荷（单位 0.01kg）----\n");

    check(scale_grams_to_payload(0.0f)    == 0,     "0 g → 0");
    check(scale_grams_to_payload(10.0f)   == 1,     "10 g → 1（1 单位 = 10g）");
    check(scale_grams_to_payload(100.0f)  == 10,    "100 g → 10");
    check(scale_grams_to_payload(1000.0f) == 100,   "1000 g → 100（1.00kg）");
    check(scale_grams_to_payload(65430.0f) == 6543, "65430 g → 6543（65.43kg）");
    check(scale_grams_to_payload(6543.0f) == 654,   "6543 g → 654（四舍五入）");
    check(scale_grams_to_payload(6545.0f) == 655,   "6545 g → 655（进位）");
    check(scale_grams_to_payload(6544.0f) == 654,   "6544 g → 654（舍去）");

    /* 边界与无效 */
    check(scale_grams_to_payload(-1.0f) == FW_WEIGHT_INVALID,  "负值 → 0xFFFF 无效");
    check(scale_grams_to_payload(-0.1f) == FW_WEIGHT_INVALID,  "小幅负值 → 无效");
    check(scale_grams_to_payload(655350.0f) == FW_WEIGHT_INVALID,
          "655350 g 超量程 → 0xFFFF（0xFFFF 必须留作无效值）");
    check(scale_grams_to_payload(655340.0f) == 65534, "655340 g 为量程上限 → 65534");

    /* 反查 */
    check(scale_payload_to_kg(6543) == 65.43f,  "6543 → 65.43 kg");
    check(scale_payload_to_kg(0) == 0.0f,       "0 → 0 kg");
    check(scale_payload_to_kg(FW_WEIGHT_INVALID) == -1.0f, "0xFFFF → -1.0f（无效约定）");
}

static void test_frame_layout(void)
{
    uint8_t f[FW_WEIGHT_FRAME];

    printf("\n---- B 段：0x02 帧字节布局 ----\n");

    check(scale_build_weight_frame(f, 65430.0f, 0x11223344u, 1) == FW_WEIGHT_FRAME,
          "帧长 == 13");
    check(f[0] == 0xAA && f[1] == 0x55,        "帧头 AA 55");
    check(f[2] == FW_DEV_SCALE,                "设备类型 == 秤(0x02)");
    check(f[3] == FW_T_WEIGHT,                 "帧类型 == 0x02");
    check(fw_get_u32(f + 4) == 0x11223344u,    "时间戳大端在偏移 4");
    check(fw_get_u16(f + 8) == 2,              "载荷长度 == 2（偏移 8）");
    check(fw_get_u16(f + 10) == 6543,          "体重大端在偏移 10");
    check(f[10] == 0x19 && f[11] == 0x8F,      "65.43kg → 0x19 0x8F");
    check(f[12] == fw_checksum(f, 12),         "校验和 == 前 12 字节累加低 8 位");
    check(fw_verify_frame(f, FW_WEIGHT_FRAME) == 0, "fw_verify_frame 通过");
}

static void test_round_trip(void)
{
    uint8_t f[FW_WEIGHT_FRAME];
    float   weights[] = { 0.0f, 10.0f, 500.0f, 1000.0f, 50000.0f,
                          65430.0f, 80000.0f, 120000.0f };
    int     i;

    printf("\n---- C 段：端到端（秤组帧 → 上位机口径解析）----\n");

    for (i = 0; i < (int)(sizeof(weights) / sizeof(weights[0])); ++i) {
        char name[80];
        float got;

        scale_build_weight_frame(f, weights[i], 1700000000u, 1);
        got = parse_like_upper_computer(f, FW_WEIGHT_FRAME);

        /* 协议精度是 0.01kg = 10g，所以允许半个单位（5g）的量化误差 */
        {
            float want = weights[i] / 1000.0f;
            float diff = got - want;
            if (diff < 0) diff = -diff;
            snprintf(name, sizeof(name),
                     "%.0f g 往返 → 上位机读到 %.2f kg（误差 %.3f kg ≤ 0.005）",
                     (double)weights[i], (double)got, (double)diff);
            check(diff <= 0.0051f, name);
        }
    }
}

static void test_uncalibrated(void)
{
    uint8_t f[FW_WEIGHT_FRAME];

    printf("\n---- D 段：未标定 / 异常读数 ----\n");

    /* 未标定：载荷必须是 0xFFFF，上位机应显示无效而不是 0 */
    scale_build_weight_frame(f, 65430.0f, 100, 0);
    check(fw_get_u16(f + 10) == FW_WEIGHT_INVALID,
          "未标定 → 载荷 0xFFFF（不冒充 0）");
    check(parse_like_upper_computer(f, FW_WEIGHT_FRAME) == -1.0f,
          "上位机解析未标定帧 → -1.0f（显示 --）");
    check(fw_verify_frame(f, FW_WEIGHT_FRAME) == 0, "未标定帧本身仍是合法帧");

    /* 异常读数（超量程）→ 也应无效 */
    scale_build_weight_frame(f, 700000.0f, 100, 1);
    check(fw_get_u16(f + 10) == FW_WEIGHT_INVALID, "超量程 → 载荷 0xFFFF");
    check(parse_like_upper_computer(f, FW_WEIGHT_FRAME) == -1.0f,
          "上位机解析超量程帧 → -1.0f");

    /* 负读数（传感器反向装配）→ 无效 */
    scale_build_weight_frame(f, -500.0f, 100, 1);
    check(fw_get_u16(f + 10) == FW_WEIGHT_INVALID, "负读数 → 载荷 0xFFFF");

    /* 越界参数 */
    check(scale_build_weight_frame(NULL, 1000.0f, 0, 1) == -1, "buf=NULL → 返回 -1");
}

static void test_ts_zero(void)
{
    uint8_t f[FW_WEIGHT_FRAME];

    printf("\n---- E 段：时间戳未校时 ----\n");

    /* 秤没有 0x03 校时流程，ts 常为 0。协议 §7 允许：PC 侧按接收时刻兜底 */
    scale_build_weight_frame(f, 65430.0f, 0, 1);
    check(fw_get_u32(f + 4) == 0, "ts=0 原样写入（0 = 未校时）");
    check(fw_verify_frame(f, FW_WEIGHT_FRAME) == 0, "ts=0 的帧仍是合法帧");
    check(parse_like_upper_computer(f, FW_WEIGHT_FRAME) == 65.43f,
          "ts=0 不影响体重解析");
}

int main(void)
{
    printf("体重秤帧组装自测（协议 v1.2，0x02 体重帧）\n");
    printf("============================================================\n");

    test_payload_conversion();
    test_frame_layout();
    test_round_trip();
    test_uncalibrated();
    test_ts_zero();

    printf("\n============================================================\n");
    printf("通过: %d    失败: %d\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("全部通过\n");
        return 0;
    }
    printf("有失败项\n");
    return 1;
}
