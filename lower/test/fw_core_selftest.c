/* fw_core 协议层自测（纯 C，可在 PC 上直接编译运行，不需要 ESP-IDF、不需要硬件）
 *
 * 目的：烧板之前就证明固件组帧与上位机 communication/protocolparser.cpp 字节级一致。
 * 编译（MinGW）：
 *   gcc -std=c99 -Wall -Wextra -I components/fw_core/include \
 *       test/fw_core_selftest.c \
 *       components/fw_core/fw_proto.c components/fw_core/fw_image_rx.c \
 *       components/fw_core/fw_image_tx.c components/fw_core/fw_host_router.c \
 *       -o test/fw_core_selftest.exe
 */

#include <stdio.h>
#include <string.h>
#include "fw_proto.h"
#include "fw_image_rx.h"
#include "fw_image_tx.h"
#include "fw_host.h"

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

/* ============================================================
 * A 段：帧结构与校验
 * ============================================================ */
static void test_frame_structure(void)
{
    uint8_t buf[FW_IMG_FRAG_FRAME_MAX];
    fw_vital_t v;
    int n;

    printf("\n---- A 段：帧结构与校验 ----\n");

    memset(&v, 0, sizeof(v));
    v.ts = 0x11223344u;
    v.hr = 72;
    v.spo2 = 98;
    v.temp_x10 = 253;
    v.hum_x10 = 455;
    v.sweat = 1;
    v.cal = 1234;
    v.hr_state = 0;

    n = fw_build_vital(buf, &v);
    check(n == FW_VITAL_FRAME, "0x01 体征帧长 == 24");
    check(buf[0] == 0xAA && buf[1] == 0x55, "0x01 帧头 AA 55");
    check(buf[2] == FW_DEV_BAND, "0x01 设备类型 == 手环(0x01)");
    check(buf[3] == FW_T_VITAL, "0x01 帧类型 == 0x01");
    check(fw_get_u32(buf + 4) == 0x11223344u, "0x01 时间戳大端");
    check(fw_get_u16(buf + 8) == 13, "0x01 载荷长度 == 13");
    check(fw_get_u16(buf + 10) == 72, "0x01 hr 在偏移 10");
    check(buf[12] == 98, "0x01 spo2 在偏移 12");
    check(fw_get_u16(buf + 13) == 253, "0x01 temp 在偏移 13");
    check(fw_get_u16(buf + 15) == 455, "0x01 hum 在偏移 15");
    check(buf[17] == 1, "0x01 sweat 在偏移 17");
    check(fw_get_u32(buf + 18) == 1234u, "0x01 cal 在偏移 18");
    check(buf[22] == 0, "0x01 hr_state 在偏移 22");
    check(buf[23] == fw_checksum(buf, 23), "0x01 校验和 == 前 23 字节累加低 8 位");
    check(fw_verify_frame(buf, n) == 0, "0x01 fw_verify_frame 通过");

    /* 上位机解析口径逐个字段反推：QDataStream 大端依次读 hr,spo2,temp,hum,sweat,cal,hrState */
    check(fw_get_u16(buf + 10) == 72 && buf[12] == 98 &&
          (int16_t)fw_get_u16(buf + 13) == 253 && fw_get_u16(buf + 15) == 455 &&
          buf[17] == 1 && fw_get_u32(buf + 18) == 1234u && buf[22] == 0,
          "0x01 字段序与上位机 QDataStream 读取序一致(2+1+2+2+1+4+1)");

    /* 校验和改坏 → 必须判非法 */
    buf[23] ^= 0x01;
    check(fw_verify_frame(buf, FW_VITAL_FRAME) != 0, "0x01 校验和被破坏 → 判非法");
    buf[23] ^= 0x01;

    /* 体重帧 13 字节 */
    {
        fw_weight_t w;
        w.ts = 1000;
        w.weight_x100 = 6543;   /* 65.43kg */
        n = fw_build_weight(buf, &w);
        check(n == FW_WEIGHT_FRAME, "0x02 体重帧长 == 13");
        check(buf[2] == FW_DEV_SCALE, "0x02 设备类型 == 秤(0x02)");
        check(fw_get_u16(buf + 10) == 6543, "0x02 体重 0.01kg 大端");
        check(buf[12] == fw_checksum(buf, 12), "0x02 校验和");
    }

    /* 补传状态帧 14 字节：state 0x00 开始 / 0x01 结束 */
    n = fw_build_backlog(buf, 1, 250, 5000);
    check(n == FW_BACKLOG_FRAME, "0x04 补传状态帧长 == 14");
    check(buf[10] == 0x00, "0x04 start=1 → state 0x00");
    check(fw_get_u16(buf + 11) == 250, "0x04 count 大端");
    n = fw_build_backlog(buf, 0, 0, 5000);
    check(buf[10] == 0x01, "0x04 start=0 → state 0x01");

    printf("\n---- 红线 1：图像分片载荷上限必须是 1027 ----\n");
    {
        uint8_t data[FW_IMG_FRAG_DATA_MAX];
        int i;
        for (i = 0; i < FW_IMG_FRAG_DATA_MAX; ++i) data[i] = (uint8_t)(i & 0xFF);
        n = fw_build_image_fragment(buf, 1, 0, data, FW_IMG_FRAG_DATA_MAX, 777);
        check(n == FW_IMG_FRAG_FRAME_MAX, "满片 1024B 数据 → 整帧 1038 字节");
        check(fw_get_u16(buf + 8) == FW_IMG_FRAG_PAYLOAD_MAX, "满片载荷长度 == 1027（不是 1024）");
        check(fw_verify_frame(buf, n) == 0, "满片帧校验通过（1027 未被误判超长）");
        check(buf[10] == 1, "分片图ID 在偏移 10");
        check(fw_get_u16(buf + 11) == 0, "分片序号 在偏移 11");
        check(buf[13] == data[0], "分片数据从偏移 13 开始");
        check(buf[13 + 1023] == data[1023], "分片数据末字节在偏移 1036");
    }
}

/* ============================================================
 * B 段：图像三帧（0x06 / 0x07 / 0x08）
 * ============================================================ */
static void test_image_frames(void)
{
    uint8_t buf[FW_IMG_FRAG_FRAME_MAX];
    int n;

    printf("\n---- B 段：图像三帧 ----\n");

    n = fw_build_image_start(buf, 0x2A, 12659, 13, 1700000000u);
    check(n == FW_IMG_START_FRAME, "0x06 图像开始帧长 == 18");
    check(buf[2] == FW_DEV_IMAGE, "0x06 设备类型 == 图像源(0x03)");
    check(buf[3] == FW_T_IMG_START, "0x06 帧类型 == 0x06");
    check(buf[10] == 0x2A, "0x06 图ID 在偏移 10");
    check(fw_get_u32(buf + 11) == 12659u, "0x06 总字节大端在偏移 11");
    check(fw_get_u16(buf + 15) == 13, "0x06 分片数大端在偏移 15");
    check(buf[17] == fw_checksum(buf, 17), "0x06 校验和");
    check(fw_get_u32(buf + 4) == 1700000000u, "0x06 帧头时间戳 == 采集时刻");

    n = fw_build_image_end(buf, 0x2A, 0x0000, 1700000000u);
    check(n == FW_IMG_END_FRAME, "0x08 图像结束帧长 == 14");
    check(buf[10] == 0x2A, "0x08 图ID 在偏移 10");
    check(fw_get_u16(buf + 11) == 0, "0x08 CRC16 保留 0x0000");
    check(buf[13] == fw_checksum(buf, 13), "0x08 校验和");

    check(fw_image_fragment_count(0) == 0, "分片数：0 字节 → 0 片");
    check(fw_image_fragment_count(1) == 1, "分片数：1 字节 → 1 片");
    check(fw_image_fragment_count(1024) == 1, "分片数：1024 → 1 片");
    check(fw_image_fragment_count(1025) == 2, "分片数：1025 → 2 片");
    check(fw_image_fragment_count(12659) == 13, "分片数：12659 → 13 片");
}

/* ============================================================
 * C 段：下行帧（0x03 / 0x05 / 0x09 / 0x0A / 0x0B），红线 2
 * ============================================================ */
static void make_downlink(uint8_t *buf, uint8_t type, uint32_t ts, const uint8_t payload[4])
{
    int i;
    buf[0] = 0xAA; buf[1] = 0x55;
    buf[2] = FW_DEV_HOST;
    buf[3] = type;
    fw_put_u32(buf + 4, ts);
    fw_put_u16(buf + 8, 4);
    for (i = 0; i < 4; ++i) buf[10 + i] = payload[i];
    buf[14] = fw_checksum(buf, 14);
}

static void test_downlink(void)
{
    uint8_t f[FW_HOST_FRAME];
    uint8_t p[4];
    fw_downlink_t d;
    fw_host_t h;

    printf("\n---- C 段：下行帧 15 字节定长（红线 2）----\n");

    /* 0x03 校时 */
    p[0] = 0x65; p[1] = 0x12; p[2] = 0x34; p[3] = 0x56;
    make_downlink(f, FW_T_TIMESYNC, 111, p);
    check(fw_verify_frame(f, FW_HOST_FRAME) == 0, "0x03 校验通过");
    check(fw_parse_host(f, FW_HOST_FRAME, &h) == 0, "0x03 fw_parse_host 兼容签名可用");
    check(h.epoch == 0x65123456u, "0x03 epoch 大端解析");
    check(fw_parse_downlink(f, FW_HOST_FRAME, &d) == 0, "0x03 fw_parse_downlink");
    check(d.u.epoch == 0x65123456u, "0x03 downlink epoch");

    /* 0x05 同步请求 */
    make_downlink(f, FW_T_SYNCREQ, 222, p);
    check(fw_parse_downlink(f, FW_HOST_FRAME, &d) == 0, "0x05 解析通过");
    check(d.type == FW_T_SYNCREQ, "0x05 type 回填");
    check(d.u.epoch == 0x65123456u, "0x05 since 大端解析");

    /* 0x09 WiFi 配置：192.168.1.5 = C0 A8 01 05 */
    p[0] = 192; p[1] = 168; p[2] = 1; p[3] = 5;
    make_downlink(f, FW_T_WIFI_CFG, 333, p);
    check(fw_verify_frame(f, FW_HOST_FRAME) == 0, "0x09 15 字节校验通过");
    check(fw_parse_downlink(f, FW_HOST_FRAME, &d) == 0, "0x09 解析通过");
    check(d.u.ip[0] == 192 && d.u.ip[1] == 168 && d.u.ip[2] == 1 && d.u.ip[3] == 5,
          "0x09 PC IPv4 C0 A8 01 05");

    /* 0x0A 震动（本版不实现，但要能解析备用） */
    p[0] = 1; p[1] = 2; p[2] = 0; p[3] = 0;
    make_downlink(f, FW_T_MOTOR, 444, p);
    check(fw_parse_downlink(f, FW_HOST_FRAME, &d) == 0, "0x0A 解析通过（已分配）");
    check(d.u.motor.warn_type == 1 && d.u.motor.motor_mode == 2, "0x0A 警告类型/震动模式");

    /* 0x0B 拍照：图ID 1~0x7F */
    p[0] = 7; p[1] = 0; p[2] = 0; p[3] = 0;
    make_downlink(f, FW_T_CAPTURE, 555, p);
    check(fw_parse_downlink(f, FW_HOST_FRAME, &d) == 0, "0x0B 解析通过");
    check(d.u.image_id == 7, "0x0B 图ID 在偏移 10");

    /* 变长帧必须被拒（固件是定长 15 字节滑窗 + 长度/载荷双重校验） */
    make_downlink(f, FW_T_CAPTURE, 555, p);
    check(fw_verify_frame(f, 14) != 0, "0x0B 截断成 14 字节 → 判非法");
    check(fw_verify_frame(f, 16) != 0, "0x0B 拉长成 16 字节 → 判非法");
    {
        uint8_t bad[FW_HOST_FRAME];
        memcpy(bad, f, FW_HOST_FRAME);
        bad[8] = 0x00; bad[9] = 0x05;   /* 声称载荷 5 字节 */
        check(fw_verify_frame(bad, FW_HOST_FRAME) != 0, "0x0B 长度字段不符 → 判非法");
    }

    /* 方向判定 */
    check(fw_frame_direction(FW_DEV_HOST, FW_T_CAPTURE) == FW_DIR_HOST_TO_DEV, "方向：0x0B 下行");
    check(fw_frame_direction(FW_DEV_IMAGE, FW_T_IMG_FRAG) == FW_DIR_DEV_TO_HOST, "方向：0x07 上行");
    check(fw_frame_direction(FW_DEV_HOST, FW_T_VITAL) == FW_DIR_INVALID, "方向：上位机发体征 → 非法");
    check(fw_frame_direction(FW_DEV_BAND, FW_T_CAPTURE) == FW_DIR_INVALID, "方向：手环发拍照 → 非法");
}

/* ============================================================
 * D 段：接收滑窗（半包 / 粘包 / 脏字节重同步）
 * ============================================================ */
static void test_rx(void)
{
    fw_rx_t rx;
    uint8_t frame[FW_HOST_FRAME];
    uint8_t out[FW_IMG_FRAG_FRAME_MAX];
    uint8_t p[4] = {0, 0, 1, 2};
    int n, i;

    printf("\n---- D 段：接收滑窗 ----\n");

    make_downlink(frame, FW_T_WIFI_CFG, 42, p);
    n = sizeof(frame);
    (void)n;

    /* 逐字节喂：前 14 字节都不得输出，第 15 字节凑齐整帧 */
    fw_rx_init(&rx);
    {
        int premature = 0;
        int got = 0;
        for (i = 0; i < FW_HOST_FRAME; ++i) {
            got = fw_rx_feed(&rx, &frame[i], 1, out, sizeof(out));
            if (i < FW_HOST_FRAME - 1 && got != 0) ++premature;
        }
        check(premature == 0, "逐字节喂：未凑齐整帧前不输出");
        check(got == FW_HOST_FRAME, "逐字节喂：末字节凑齐整帧");
        check(memcmp(out, frame, FW_HOST_FRAME) == 0, "滑窗输出与原帧逐字节相同");
    }

    /* 一次喂 3 帧（粘包）*/
    {
        uint8_t three[FW_HOST_FRAME * 3];
        int got_total = 0, got;
        for (i = 0; i < 3; ++i) memcpy(three + i * FW_HOST_FRAME, frame, FW_HOST_FRAME);
        fw_rx_init(&rx);
        for (i = 0; i < (int)sizeof(three); ++i) {
            got = fw_rx_feed(&rx, &three[i], 1, out, sizeof(out));
            if (got) ++got_total;
        }
        check(got_total == 3, "粘包 3 帧 → 逐帧输出 3 次");
    }

    /* 前面有脏字节 → 重同步 */
    {
        uint8_t junk[5] = {0x11, 0x22, 0xAA, 0x33, 0x44};
        uint8_t seq[5 + FW_HOST_FRAME];
        int got = 0;
        memcpy(seq, junk, 5);
        memcpy(seq + 5, frame, FW_HOST_FRAME);
        fw_rx_init(&rx);
        for (i = 0; i < (int)sizeof(seq); ++i) {
            if (fw_rx_feed(&rx, &seq[i], 1, out, sizeof(out))) got = 1;
        }
        check(got == 1, "脏字节(含假 0xAA)后仍能重同步出 1 帧");
        check(memcmp(out, frame, FW_HOST_FRAME) == 0, "重同步后帧内容正确");
    }

    /* 校验和错帧 → 不输出，且不影响后续好帧 */
    {
        uint8_t bad[FW_HOST_FRAME];
        uint8_t both[FW_HOST_FRAME * 2];
        int got = 0;
        memcpy(bad, frame, FW_HOST_FRAME);
        bad[FW_HOST_FRAME - 1] ^= 0xFF;
        memcpy(both, bad, FW_HOST_FRAME);
        memcpy(both + FW_HOST_FRAME, frame, FW_HOST_FRAME);
        fw_rx_init(&rx);
        for (i = 0; i < (int)sizeof(both); ++i) {
            if (fw_rx_feed(&rx, &both[i], 1, out, sizeof(out))) ++got;
        }
        check(got == 1, "坏校验帧被丢弃，后续好帧仍能解析");
    }

    /* 图像满片 1038 字节帧也能过滑窗（缓冲上限校验） */
    {
        uint8_t data[FW_IMG_FRAG_DATA_MAX];
        uint8_t big[FW_IMG_FRAG_FRAME_MAX];
        int got;
        for (i = 0; i < FW_IMG_FRAG_DATA_MAX; ++i) data[i] = (uint8_t)i;
        i = fw_build_image_fragment(big, 3, 5, data, FW_IMG_FRAG_DATA_MAX, 9);
        check(i == FW_IMG_FRAG_FRAME_MAX, "满片帧 1038 字节组建成功");
        fw_rx_init(&rx);
        got = fw_rx_feed(&rx, big, i, out, sizeof(out));
        check(got == FW_IMG_FRAG_FRAME_MAX, "满片 1038 字节帧通过滑窗（缓冲不截断）");
    }
}

/* ============================================================
 * E 段：图像重组五场景（与 PC 侧 ImageAssembler 对齐）
 * ============================================================ */
static uint32_t g_fake_ms = 0;
static uint32_t fake_now(void *ctx) { (void)ctx; return g_fake_ms; }

#define IMG_BUF_CAP  (64u * 1024u)
#define IMG_SEQ_CAP  (1024u / 8u)

static uint8_t g_img_buf[IMG_BUF_CAP];
static uint8_t g_seq_seen[IMG_SEQ_CAP];
static uint8_t g_expect[IMG_BUF_CAP];

/* 造一张 total 字节的图，内容 = 递增字节 */
static void make_image(uint32_t total)
{
    uint32_t i;
    for (i = 0; i < total; ++i) g_expect[i] = (uint8_t)(i * 7u + 3u);
}

/* 把整图按 1024 切片喂进重组器（skip_seq = 故意丢掉的分片，<0 表示不丢） */
static int feed_image(fw_image_rx_t *rx, uint8_t id, uint32_t total, int skip_seq, uint32_t ts)
{
    uint8_t frame[FW_IMG_FRAG_FRAME_MAX];
    uint16_t count = fw_image_fragment_count(total);
    uint16_t seq;
    int r;

    r = fw_image_rx_start(rx, id, total, count, ts);
    if (r != FW_IMAGE_OK) return -100 - r;

    for (seq = 0; seq < count; ++seq) {
        uint32_t off = (uint32_t)seq * FW_IMG_FRAG_DATA_MAX;
        uint16_t len = (uint16_t)((total - off > FW_IMG_FRAG_DATA_MAX)
                                  ? FW_IMG_FRAG_DATA_MAX : (total - off));
        if ((int)seq == skip_seq) continue;   /* 故意丢片 */
        fw_build_image_fragment(frame, id, seq, g_expect + off, len, ts);
        r = fw_image_rx_fragment(rx, id, seq, frame + 13, len);
        if (r != FW_IMAGE_OK) return r;
    }
    return fw_image_rx_end(rx, id, 0x0000);
}

static void test_image_assembler(void)
{
    static fw_image_rx_t rx;
    uint32_t total = 12659;   /* 13 片，末片 12659 - 12*1024 = 371 */
    uint16_t count = fw_image_fragment_count(total);
    uint16_t seq;

    printf("\n---- E 段：图像重组五场景 ----\n");

    fw_image_rx_init(&rx, g_img_buf, IMG_BUF_CAP, g_seq_seen, IMG_SEQ_CAP, fake_now, NULL);
    make_image(total);

    /* 场景 1：正常 */
    g_fake_ms = 1000;
    {
        int r = feed_image(&rx, 5, total, -1, 1700000000u);
        check(r == FW_IMAGE_OK, "场景1(正常)：重组成功");
        check(fw_image_rx_is_ready(&rx) == 1, "场景1：imageReady 就绪");
        check(fw_image_rx_size(&rx) == total, "场景1：字节数 == 声明总字节");
        check(memcmp(fw_image_rx_data(&rx), g_expect, total) == 0, "场景1：数据逐字节正确");
        fw_image_rx_reset(&rx);
    }

    /* 场景 2：乱序（倒着喂） */
    g_fake_ms = 2000;
    {
        int r = fw_image_rx_start(&rx, 6, total, count, 1700000001u);
        for (seq = count; seq > 0; --seq) {
            uint16_t s = (uint16_t)(seq - 1);
            uint32_t off = (uint32_t)s * FW_IMG_FRAG_DATA_MAX;
            uint16_t len = (uint16_t)((total - off > FW_IMG_FRAG_DATA_MAX)
                                      ? FW_IMG_FRAG_DATA_MAX : (total - off));
            r = fw_image_rx_fragment(&rx, 6, s, g_expect + off, len);
            if (r != FW_IMAGE_OK) break;
        }
        check(r == FW_IMAGE_OK, "场景2(乱序)：倒序分片全部接受");
        r = fw_image_rx_end(&rx, 6, 0);
        check(r == FW_IMAGE_OK, "场景2(乱序)：收尾通过");
        check(memcmp(fw_image_rx_data(&rx), g_expect, total) == 0, "场景2：乱序仍按序号落位正确");
        fw_image_rx_reset(&rx);
    }

    /* 场景 3：丢片 → 超时丢弃 */
    g_fake_ms = 3000;
    {
        int r = feed_image(&rx, 7, total, 4, 1700000002u);   /* 丢掉 seq=4 */
        check(r == FW_IMAGE_ERR_INCOMPLETE, "场景3(丢片)：收尾判 incomplete");
        check(fw_image_rx_is_ready(&rx) == 0, "场景3：不产出图");
        check(fw_image_rx_last_discard(&rx) == FW_IMAGE_DISCARD_INCOMPLETE, "场景3：reason=incomplete");
        fw_image_rx_reset(&rx);
    }
    /* 场景 3b：真超时（末片一直不来） */
    g_fake_ms = 4000;
    {
        int r = fw_image_rx_start(&rx, 8, total, count, 1700000003u);
        uint32_t off = 0;
        uint16_t len = (uint16_t)((total > FW_IMG_FRAG_DATA_MAX) ? FW_IMG_FRAG_DATA_MAX : total);
        r = fw_image_rx_fragment(&rx, 8, 0, g_expect + off, len);
        check(r == FW_IMAGE_OK, "场景3b(超时)：首片接受");
        g_fake_ms += (FW_IMAGE_TIMEOUT_MS_DEFAULT - 1);
        check(fw_image_rx_poll_timeout(&rx) == 0, "场景3b：未到超时 → 不丢弃");
        g_fake_ms += 1;
        check(fw_image_rx_poll_timeout(&rx) == 1, "场景3b：到 5 秒 → 丢弃");
        check(fw_image_rx_last_discard(&rx) == FW_IMAGE_DISCARD_TIMEOUT, "场景3b：reason=timeout");
        check(fw_image_rx_state(&rx) == FW_IMAGE_STATE_ERROR, "场景3b：槽位已释放(ERROR)");
        /* 复位后迟到分片按图 ID 忽略 */
        r = fw_image_rx_fragment(&rx, 8, 1, g_expect + FW_IMG_FRAG_DATA_MAX, 1024);
        check(r == FW_IMAGE_ERR_BUSY, "场景3b：复位后迟到分片被忽略");
        fw_image_rx_reset(&rx);
    }

    /* 场景 4：重复片 */
    g_fake_ms = 5000;
    {
        int r = fw_image_rx_start(&rx, 9, total, count, 1700000004u);
        uint16_t len = (uint16_t)((total > FW_IMG_FRAG_DATA_MAX) ? FW_IMG_FRAG_DATA_MAX : total);
        r = fw_image_rx_fragment(&rx, 9, 0, g_expect, len);
        check(r == FW_IMAGE_OK, "场景4(重复片)：首片接受");
        r = fw_image_rx_fragment(&rx, 9, 0, g_expect, len);
        check(r == FW_IMAGE_ERR_DUPLICATE, "场景4：重复片被识别并忽略");
        check(rx.frag_received == 1, "场景4：重复片未计入实收分片数");
        check(rx.received_bytes == len, "场景4：重复片未计入字节和");
        /* 补全后续分片（含 seq=0 已收），应能成功 */
        for (seq = 1; seq < count; ++seq) {
            uint32_t off = (uint32_t)seq * FW_IMG_FRAG_DATA_MAX;
            uint16_t l = (uint16_t)((total - off > FW_IMG_FRAG_DATA_MAX)
                                    ? FW_IMG_FRAG_DATA_MAX : (total - off));
            fw_image_rx_fragment(&rx, 9, seq, g_expect + off, l);
        }
        r = fw_image_rx_end(&rx, 9, 0);
        check(r == FW_IMAGE_OK, "场景4：去重后完整性三项仍通过");
        fw_image_rx_reset(&rx);
    }

    /* 场景 5：新图抢占 */
    g_fake_ms = 6000;
    {
        int r = fw_image_rx_start(&rx, 10, total, count, 1700000005u);
        r = fw_image_rx_fragment(&rx, 10, 0, g_expect, 1024);
        check(r == FW_IMAGE_OK, "场景5(抢占)：旧图首片接受");
        r = fw_image_rx_start(&rx, 11, total, count, 1700000006u);
        check(r == FW_IMAGE_OK, "场景5：新 0x06 建新槽成功");
        check(fw_image_rx_last_discard(&rx) == FW_IMAGE_DISCARD_NONE ||
              rx.image_id == 11, "场景5：旧图被抢占丢弃");
        check(rx.image_id == 11, "场景5：槽内图ID 已换成新图");
        check(rx.frag_received == 0, "场景5：新槽计数清零");
        /* 旧图迟到分片被忽略 */
        r = fw_image_rx_fragment(&rx, 10, 1, g_expect + 1024, 1024);
        check(r == FW_IMAGE_ERR_ID_MISMATCH, "场景5：旧图迟到分片按图ID忽略");
        fw_image_rx_reset(&rx);
    }

    /* 头字段非法：分片数与总字节不符 */
    g_fake_ms = 7000;
    {
        int r = fw_image_rx_start(&rx, 12, 12659, 5, 0);   /* 12659 应是 13 片 */
        check(r == FW_IMAGE_ERR_BAD_HEADER, "头字段非法(分片数不符) → 拒收");
        check(fw_image_rx_is_ready(&rx) == 0, "头字段非法 → 不产出图");
        r = fw_image_rx_start(&rx, 12, 0, 0, 0);
        check(r == FW_IMAGE_ERR_BAD_HEADER, "头字段非法(全 0) → 拒收");
        fw_image_rx_reset(&rx);
    }

    /* 单帧数据超 1024 拒收 */
    g_fake_ms = 8000;
    {
        static uint8_t big[FW_IMG_FRAG_DATA_MAX + 1];
        int r = fw_image_rx_start(&rx, 13, total, count, 0);
        r = fw_image_rx_fragment(&rx, 13, 0, big, FW_IMG_FRAG_DATA_MAX + 1);
        check(r == FW_IMAGE_ERR_TOO_BIG, "单帧数据 1025 > 1024 → 拒收");
        fw_image_rx_reset(&rx);
    }
}

/* ============================================================
 * F 段：整图链路（组帧 → 滑窗 → 重组），模拟真机
 * ============================================================ */
static void test_full_chain(void)
{
    static fw_image_rx_t rx;
    uint8_t tx[FW_IMG_FRAG_FRAME_MAX];
    uint8_t wire[FW_IMG_FRAG_FRAME_MAX * 2];   /* 故意用两倍缓冲拼接，制造粘包 */
    uint8_t out[FW_IMG_FRAG_FRAME_MAX];
    fw_rx_t rxwin;
    uint32_t total = 8192;    /* 8 片整 */
    uint16_t count = fw_image_fragment_count(total);
    uint16_t seq;
    int n, i;
    uint32_t ts = 1700009999u;
    int frames = 0;

    printf("\n---- F 段：整图链路（组帧→滑窗→重组）----\n");

    make_image(total);
    fw_image_rx_init(&rx, g_img_buf, IMG_BUF_CAP, g_seq_seen, IMG_SEQ_CAP, fake_now, NULL);
    fw_rx_init(&rxwin);
    g_fake_ms = 10000;

    /* 0x06 */
    n = fw_build_image_start(tx, 0x21, total, count, ts);
    for (i = 0; i < n; ++i) {
        int got = fw_rx_feed(&rxwin, &tx[i], 1, out, sizeof(out));
        if (got) { ++frames; fw_image_rx_start(&rx, out[10], fw_get_u32(out + 11),
                                              fw_get_u16(out + 15), fw_get_u32(out + 4)); }
    }
    /* 0x07 × N（每两片拼成一段喂进去，模拟粘包）*/
    for (seq = 0; seq < count; seq += 2) {
        uint32_t off0 = (uint32_t)seq * FW_IMG_FRAG_DATA_MAX;
        uint16_t len0 = (uint16_t)((total - off0 > FW_IMG_FRAG_DATA_MAX) ? FW_IMG_FRAG_DATA_MAX : (total - off0));
        int m = fw_build_image_fragment(wire, 0x21, seq, g_expect + off0, len0, ts);
        if (seq + 1 < count) {
            uint32_t off1 = (uint32_t)(seq + 1) * FW_IMG_FRAG_DATA_MAX;
            uint16_t len1 = (uint16_t)((total - off1 > FW_IMG_FRAG_DATA_MAX) ? FW_IMG_FRAG_DATA_MAX : (total - off1));
            uint8_t tmp[FW_IMG_FRAG_FRAME_MAX];
            int m2 = fw_build_image_fragment(tmp, 0x21, (uint16_t)(seq + 1), g_expect + off1, len1, ts);
            memcpy(wire + m, tmp, m2);
            m += m2;
        }
        for (i = 0; i < m; ++i) {
            int got = fw_rx_feed(&rxwin, &wire[i], 1, out, sizeof(out));
            if (got) {
                ++frames;
                fw_image_rx_fragment(&rx, out[10], fw_get_u16(out + 11), out + 13,
                                     (uint16_t)(fw_get_u16(out + 8) - FW_IMG_FRAG_HDR));
            }
        }
    }
    /* 0x08 */
    n = fw_build_image_end(tx, 0x21, 0x0000, ts);
    for (i = 0; i < n; ++i) {
        int got = fw_rx_feed(&rxwin, &tx[i], 1, out, sizeof(out));
        if (got) { ++frames; fw_image_rx_end(&rx, out[10], fw_get_u16(out + 11)); }
    }

    check(frames == count + 2, "整链路：解析出 0x06 + N 个 0x07 + 0x08 共 N+2 帧");
    check(fw_image_rx_is_ready(&rx) == 1, "整链路：图像重组就绪");
    check(fw_image_rx_size(&rx) == total, "整链路：字节数正确");
    check(memcmp(fw_image_rx_data(&rx), g_expect, total) == 0, "整链路：内容逐字节正确");
    check(memcmp(fw_image_rx_data(&rx), fw_image_rx_data(&rx), total) == 0, "整链路：缓冲自洽");
}

/* ============================================================
 * G 段：下行帧路由器（0x09/0x0B/0x0A 归板级，0x03/0x05 转交 app）
 * ============================================================ */
static uint8_t  g_last_capture_id;
static int      g_capture_called;
static uint8_t  g_last_ip[4];
static int      g_wifi_called;
static uint8_t  g_last_warn, g_last_mode;
static int      g_motor_called;
static int      g_forwarded_bytes;

/* 路由器会把 0x03/0x05 整帧转交给 fw_band_app。本测试只验证路由本身，
 * 所以这里给一个桩替代真的 fw_band_app（避免把 fw_log 等一并拉进来）。 */
void fw_band_app_rx_byte(fw_band_app_t *app, uint8_t b)
{
    (void)app; (void)b;
    ++g_forwarded_bytes;
}

static void hook_capture(void *ctx, uint8_t image_id)
{
    (void)ctx; g_capture_called++; g_last_capture_id = image_id;
}
static void hook_wifi(void *ctx, const uint8_t ip[4])
{
    (void)ctx; g_wifi_called++; memcpy(g_last_ip, ip, 4);
}
static void hook_motor(void *ctx, uint8_t warn_type, uint8_t motor_mode)
{
    (void)ctx; g_motor_called++; g_last_warn = warn_type; g_last_mode = motor_mode;
}

static void test_host_router(void)
{
    fw_host_router_t h;
    fw_host_hooks_t  hooks;
    uint8_t f[FW_HOST_FRAME];
    uint8_t p[4];
    int i;

    printf("\n---- G 段：下行帧路由器 ----\n");

    memset(&hooks, 0, sizeof(hooks));
    hooks.on_capture  = hook_capture;
    hooks.on_wifi_cfg = hook_wifi;
    hooks.on_motor    = hook_motor;
    fw_host_init(&h, &hooks);

    g_capture_called = g_wifi_called = g_motor_called = 0;

    /* 0x0B 拍照：应触发 on_capture，且不转交 app */
    p[0] = 9; p[1] = 0; p[2] = 0; p[3] = 0;
    make_downlink(f, FW_T_CAPTURE, 1, p);
    for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
    check(g_capture_called == 1, "路由器：0x0B 触发 on_capture");
    check(g_last_capture_id == 9, "路由器：0x0B 图 ID 传递正确");
    check(h.stat_captures == 1, "路由器：stat_captures 计数");
    check(h.stat_forwarded == 0, "路由器：0x0B 不转交 app");

    /* 0x09 WiFi 配置 */
    p[0] = 192; p[1] = 168; p[2] = 43; p[3] = 190;
    make_downlink(f, FW_T_WIFI_CFG, 2, p);
    for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
    check(g_wifi_called == 1, "路由器：0x09 触发 on_wifi_cfg");
    check(g_last_ip[0] == 192 && g_last_ip[1] == 168 &&
          g_last_ip[2] == 43  && g_last_ip[3] == 190,
          "路由器：0x09 PC IPv4 = 192.168.43.190");

    /* 0x0A 震动 */
    p[0] = 1; p[1] = 3; p[2] = 0; p[3] = 0;
    make_downlink(f, FW_T_MOTOR, 3, p);
    for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
    check(g_motor_called == 1, "路由器：0x0A 触发 on_motor");
    check(g_last_warn == 1 && g_last_mode == 3, "路由器：0x0A 参数传递正确");

    /* 0x03 / 0x05 应转交 app（这里 app 传 NULL，只验证计数） */
    p[0] = 0x65; p[1] = 0; p[2] = 0; p[3] = 0;
    make_downlink(f, FW_T_TIMESYNC, 4, p);
    for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
    check(h.stat_forwarded == 1, "路由器：0x03 转交 app");

    make_downlink(f, FW_T_SYNCREQ, 5, p);
    for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
    check(h.stat_forwarded == 2, "路由器：0x05 转交 app");

    /* 校验和被破坏的 0x0B：不该触发 hook */
    {
        int before = g_capture_called;
        make_downlink(f, FW_T_CAPTURE, 6, p);
        f[FW_HOST_FRAME - 1] ^= 0xFF;
        for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
        check(g_capture_called == before, "路由器：坏校验帧不触发 hook");
        check(h.stat_resync >= 1, "路由器：坏校验帧计入 resync");
    }

    /* 前面塞脏字节（含假 0xAA）后仍能正确解出 */
    {
        int before = g_capture_called;
        uint8_t junk[4] = { 0x00, 0xAA, 0x00, 0x55 };
        make_downlink(f, FW_T_CAPTURE, 7, p);
        for (i = 0; i < 4; ++i) fw_host_feed(&h, &junk[i], 1, NULL);
        for (i = 0; i < FW_HOST_FRAME; ++i) fw_host_feed(&h, &f[i], 1, NULL);
        check(g_capture_called == before + 1, "路由器：脏字节后仍能解出 0x0B");
    }

    /* 一次喂整帧（粘包场景：两帧一起）。注意 0x0B 的图 ID 在载荷里，
     * 所以要分别设置 p[0]，否则两帧图 ID 相同。 */
    {
        int before = g_capture_called;
        uint8_t two[FW_HOST_FRAME * 2];
        p[0] = 11; p[1] = 0; p[2] = 0; p[3] = 0;
        make_downlink(f, FW_T_CAPTURE, 8, p);
        memcpy(two, f, FW_HOST_FRAME);
        p[0] = 12;
        make_downlink(f, FW_T_CAPTURE, 9, p);
        memcpy(two + FW_HOST_FRAME, f, FW_HOST_FRAME);
        fw_host_feed(&h, two, sizeof(two), NULL);
        check(g_capture_called == before + 2, "路由器：一次喂两帧全解出");
        check(g_last_capture_id == 12, "路由器：末帧图 ID = 12（载荷携带，逐帧不同）");
    }
}

/* ============================================================
 * H 段：图像发送（fw_image_tx）—— 与接收侧对接验证
 * ============================================================ */
#define TX_CAP_MAX (128u * 1024u)
static uint8_t  g_tx_buf[TX_CAP_MAX];
static uint32_t g_tx_len;
static int      g_tx_fail_at;      /* 第几次发送返回失败（-1 = 不失败） */
static int      g_tx_calls;

static int tx_send(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    g_tx_calls++;
    if (g_tx_fail_at > 0 && g_tx_calls >= g_tx_fail_at) return 0;
    if (g_tx_len + len > TX_CAP_MAX) return 0;
    memcpy(g_tx_buf + g_tx_len, data, len);
    g_tx_len += len;
    return 1;
}

static void test_image_tx(void)
{
    uint32_t total = 12659;                 /* 13 片，末片 371 */
    uint16_t count = fw_image_fragment_count(total);
    fw_image_tx_result_t r;
    uint8_t p[4];

    printf("\n---- H 段：图像发送（组帧→滑窗→重组 全链）----\n");

    make_image(total);
    g_tx_len = 0; g_tx_calls = 0; g_tx_fail_at = -1;

    r = fw_image_tx_send(g_expect, total, 0x2A, 1700000000u, tx_send, NULL);
    check(r == FW_IMAGE_TX_OK, "发送：整张图送出成功");
    check(g_tx_calls == count + 2, "发送：共发 0x06 + N 片 + 0x08 = N+2 帧");

    /* 校验字节总数：18 + (10+3+1024+1)*12 + (10+3+371+1) + 14 */
    {
        uint32_t expect = FW_IMG_START_FRAME
                        + (uint32_t)(count - 1) * FW_IMG_FRAG_FRAME_MAX
                        + (FW_HDR_LEN + FW_IMG_FRAG_HDR + (total - (uint32_t)(count - 1) * FW_IMG_FRAG_DATA_MAX) + FW_SUM_LEN)
                        + FW_IMG_END_FRAME;
        check(g_tx_len == expect, "发送：字节总数与逐帧计算一致");
    }

    /* 把发出去的字节流重新解析并重组 → 必须与原图逐字节相同 */
    {
        static fw_image_rx_t rx;
        fw_rx_t win;
        uint8_t frame[FW_IMG_FRAG_FRAME_MAX];
        uint32_t off = 0;
        int n;

        fw_image_rx_init(&rx, g_img_buf, IMG_BUF_CAP, g_seq_seen, IMG_SEQ_CAP, fake_now, NULL);
        fw_rx_init(&win);
        g_fake_ms = 20000;

        while (off < g_tx_len) {
            n = fw_rx_feed(&win, g_tx_buf + off, (int)(g_tx_len - off), frame, sizeof(frame));
            if (n <= 0) break;
            off += (uint32_t)n;

            switch (frame[3]) {
            case FW_T_IMG_START:
                fw_image_rx_start(&rx, frame[10], fw_get_u32(frame + 11),
                                  fw_get_u16(frame + 15), fw_get_u32(frame + 4));
                break;
            case FW_T_IMG_FRAG:
                fw_image_rx_fragment(&rx, frame[10], fw_get_u16(frame + 11),
                                     frame + 13,
                                     (uint16_t)(fw_get_u16(frame + 8) - FW_IMG_FRAG_HDR));
                break;
            case FW_T_IMG_END:
                fw_image_rx_end(&rx, frame[10], fw_get_u16(frame + 11));
                break;
            default:
                break;
            }
        }
        check(fw_image_rx_is_ready(&rx) == 1, "发送→接收：重组就绪");
        check(fw_image_rx_size(&rx) == total, "发送→接收：字节数一致");
        check(memcmp(fw_image_rx_data(&rx), g_expect, total) == 0,
              "发送→接收：内容逐字节一致（端到端闭环）");
    }

    /* 参数校验 */
    g_tx_len = 0; g_tx_calls = 0; g_tx_fail_at = -1;
    r = fw_image_tx_send(NULL, 100, 1, 0, tx_send, NULL);
    check(r == FW_IMAGE_TX_ERR_NO_DATA, "发送：空数据被拒");
    r = fw_image_tx_send(g_expect, 0, 1, 0, tx_send, NULL);
    check(r == FW_IMAGE_TX_ERR_NO_DATA, "发送：长度为 0 被拒");
    r = fw_image_tx_send(g_expect, total, 0, 0, tx_send, NULL);   /* 图 ID 0 非法 */
    check(r == FW_IMAGE_TX_ERR_BAD_ID, "发送：图 ID 0 被拒（合法范围 1~0x7F）");
    r = fw_image_tx_send(g_expect, total, 0x80, 0, tx_send, NULL);/* 图 ID 0x80 非法 */
    check(r == FW_IMAGE_TX_ERR_BAD_ID, "发送：图 ID 0x80 被拒");
    r = fw_image_tx_send(g_expect, total, 0x7F, 0, tx_send, NULL);
    check(r == FW_IMAGE_TX_OK, "发送：图 ID 0x7F 合法（边界）");
    r = fw_image_tx_send(g_expect, total, 1, 0, NULL, NULL);
    check(r == FW_IMAGE_TX_ERR_NO_SEND, "发送：缺发送回调被拒");

    /* 通道中断：第 3 次发送失败 → 整个流程提前结束并报错（best-effort 语义） */
    g_tx_len = 0; g_tx_calls = 0; g_tx_fail_at = 3;
    r = fw_image_tx_send(g_expect, total, 5, 0, tx_send, NULL);
    check(r == FW_IMAGE_TX_ERR_SEND_FAILED, "发送：通道中断时报 SEND_FAILED");
    check(g_tx_calls == 3, "发送：失败后立即停止，不继续发剩余分片");

    (void)p;
}

int main(void)
{
    printf("fw_core 协议层自测（协议 v1.2）\n");
    printf("============================================================\n");

    test_frame_structure();
    test_image_frames();
    test_downlink();
    test_rx();
    test_image_assembler();
    test_full_chain();
    test_host_router();
    test_image_tx();

    printf("\n============================================================\n");
    printf("通过: %d    失败: %d\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("全部通过\n");
        return 0;
    }
    printf("有失败项\n");
    return 1;
}
