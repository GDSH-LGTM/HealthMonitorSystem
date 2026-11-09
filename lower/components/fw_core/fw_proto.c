#include "fw_proto.h"

/* 协议 v1.2 组帧 / 解析（纯 C，无平台依赖）
 * 字节布局必须与上位机 communication/protocolparser.cpp 完全一致 */

/* ==================== 基础工具 ==================== */

void fw_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

void fw_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

uint16_t fw_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t fw_get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

uint8_t fw_checksum(const uint8_t *data, int len)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i < len; ++i) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFFu);
}

/* ==================== 帧结构判定 ==================== */

fw_frame_dir_t fw_frame_direction(uint8_t dev, uint8_t type)
{
    if (dev == FW_DEV_HOST) {
        switch (type) {
        case FW_T_TIMESYNC:
        case FW_T_SYNCREQ:
        case FW_T_WIFI_CFG:
        case FW_T_MOTOR:
        case FW_T_CAPTURE:
            return FW_DIR_HOST_TO_DEV;
        default:
            return FW_DIR_INVALID;
        }
    }
    if (dev == FW_DEV_BAND || dev == FW_DEV_SCALE || dev == FW_DEV_IMAGE) {
        switch (type) {
        case FW_T_VITAL:
        case FW_T_WEIGHT:
        case FW_T_BACKLOG:
        case FW_T_IMG_START:
        case FW_T_IMG_FRAG:
        case FW_T_IMG_END:
            return FW_DIR_DEV_TO_HOST;
        default:
            return FW_DIR_INVALID;
        }
    }
    return FW_DIR_INVALID;
}

int fw_frame_expected_len(uint8_t type, int payload_len)
{
    switch (type) {
    case FW_T_VITAL:     return (payload_len == FW_VITAL_PAYLOAD)   ? FW_VITAL_FRAME   : -1;
    case FW_T_WEIGHT:    return (payload_len == FW_WEIGHT_PAYLOAD)  ? FW_WEIGHT_FRAME  : -1;
    case FW_T_BACKLOG:   return (payload_len == FW_BACKLOG_PAYLOAD) ? FW_BACKLOG_FRAME : -1;
    case FW_T_TIMESYNC:
    case FW_T_SYNCREQ:
    case FW_T_WIFI_CFG:
    case FW_T_MOTOR:
    case FW_T_CAPTURE:   return (payload_len == FW_HOST_PAYLOAD)    ? FW_HOST_FRAME    : -1;
    case FW_T_IMG_START: return (payload_len == FW_IMG_START_PAYLOAD) ? FW_IMG_START_FRAME : -1;
    case FW_T_IMG_END:   return (payload_len == FW_IMG_END_PAYLOAD)   ? FW_IMG_END_FRAME   : -1;
    case FW_T_IMG_FRAG:
        if (payload_len < FW_IMG_FRAG_HDR + 1 || payload_len > FW_IMG_FRAG_PAYLOAD_MAX) {
            return -1;
        }
        return FW_HDR_LEN + payload_len + FW_SUM_LEN;
    default:
        return -1;
    }
}

int fw_verify_frame(const uint8_t *frame, int len)
{
    uint8_t type;
    int payload_len;

    if (len < FW_HDR_LEN + FW_SUM_LEN) return -1;
    if (frame[0] != FW_HEAD0 || frame[1] != FW_HEAD1) return -1;

    type        = frame[3];
    payload_len = (int)fw_get_u16(frame + 8);
    if (fw_frame_expected_len(type, payload_len) != len) return -1;
    if (frame[len - 1] != fw_checksum(frame, len - 1)) return -1;
    return 0;
}

/* 组帧公共头 */
static void put_header(uint8_t *buf, uint8_t dev, uint8_t type,
                       uint32_t ts, uint16_t payload_len)
{
    buf[0] = FW_HEAD0;
    buf[1] = FW_HEAD1;
    buf[2] = dev;
    buf[3] = type;
    fw_put_u32(buf + 4, ts);
    fw_put_u16(buf + 8, payload_len);
}

/* ==================== 设备 → 上位机 ==================== */

int fw_build_vital(uint8_t *buf, const fw_vital_t *v)
{
    put_header(buf, FW_DEV_BAND, FW_T_VITAL, v->ts, FW_VITAL_PAYLOAD);
    fw_put_u16(buf + 10, v->hr);        /* 10~11 */
    buf[12] = v->spo2;                  /* 12    */
    fw_put_u16(buf + 13, v->temp_x10);  /* 13~14 */
    fw_put_u16(buf + 15, v->hum_x10);   /* 15~16 */
    buf[17] = v->sweat;                 /* 17    */
    fw_put_u32(buf + 18, v->cal);       /* 18~21 */
    buf[22] = v->hr_state;              /* 22    */
    buf[23] = fw_checksum(buf, 23);     /* 23    */
    return FW_VITAL_FRAME;
}

int fw_build_weight(uint8_t *buf, const fw_weight_t *w)
{
    put_header(buf, FW_DEV_SCALE, FW_T_WEIGHT, w->ts, FW_WEIGHT_PAYLOAD);
    fw_put_u16(buf + 10, w->weight_x100);
    buf[12] = fw_checksum(buf, 12);
    return FW_WEIGHT_FRAME;
}

int fw_build_backlog(uint8_t *buf, int start, uint16_t count, uint32_t ts)
{
    put_header(buf, FW_DEV_BAND, FW_T_BACKLOG, ts, FW_BACKLOG_PAYLOAD);
    buf[10] = (uint8_t)(start ? 0x00 : 0x01);   /* 0x00 开始 / 0x01 结束 */
    fw_put_u16(buf + 11, count);
    buf[13] = fw_checksum(buf, 13);
    return FW_BACKLOG_FRAME;
}

/* ==================== 图像组帧（通道 B） ==================== */

uint16_t fw_image_fragment_count(uint32_t total_bytes)
{
    if (total_bytes == 0) return 0;
    return (uint16_t)((total_bytes + FW_IMG_FRAG_DATA_MAX - 1) / FW_IMG_FRAG_DATA_MAX);
}

int fw_build_image_start(uint8_t *buf, uint8_t image_id, uint32_t total_bytes,
                         uint16_t fragment_count, uint32_t ts)
{
    put_header(buf, FW_DEV_IMAGE, FW_T_IMG_START, ts, FW_IMG_START_PAYLOAD);
    buf[10] = image_id;                                   /* 10    */
    fw_put_u32(buf + 11, total_bytes);                     /* 11~14 */
    fw_put_u16(buf + 15, fragment_count);                  /* 15~16 */
    buf[17] = fw_checksum(buf, 17);                        /* 17    */
    return FW_IMG_START_FRAME;
}

int fw_build_image_fragment(uint8_t *buf, uint8_t image_id, uint16_t seq,
                            const uint8_t *data, uint16_t data_len, uint32_t ts)
{
    int payload_len;
    int frame_len;
    int i;

    if (data_len == 0 || data_len > FW_IMG_FRAG_DATA_MAX) return -1;

    payload_len = FW_IMG_FRAG_HDR + (int)data_len;
    frame_len   = FW_HDR_LEN + payload_len + FW_SUM_LEN;

    put_header(buf, FW_DEV_IMAGE, FW_T_IMG_FRAG, ts, (uint16_t)payload_len);
    buf[10] = image_id;
    fw_put_u16(buf + 11, seq);
    for (i = 0; i < (int)data_len; ++i) {
        buf[13 + i] = data[i];
    }
    buf[frame_len - 1] = fw_checksum(buf, frame_len - 1);
    return frame_len;
}

int fw_build_image_end(uint8_t *buf, uint8_t image_id, uint16_t crc16, uint32_t ts)
{
    put_header(buf, FW_DEV_IMAGE, FW_T_IMG_END, ts, FW_IMG_END_PAYLOAD);
    buf[10] = image_id;
    fw_put_u16(buf + 11, crc16);      /* 本版填 0x0000，接收方不校验 */
    buf[13] = fw_checksum(buf, 13);
    return FW_IMG_END_FRAME;
}

/* ==================== 上位机 → 设备 解析 ==================== */

int fw_parse_host(const uint8_t *frame, int len, fw_host_t *out)
{
    if (out == 0) return -1;
    if (len != FW_HOST_FRAME) return -1;
    if (frame[2] != FW_DEV_HOST) return -1;
    if (frame[3] != FW_T_TIMESYNC && frame[3] != FW_T_SYNCREQ) return -1;
    if (fw_verify_frame(frame, len) != 0) return -1;

    out->type  = frame[3];
    out->ts    = fw_get_u32(frame + 4);
    out->epoch = fw_get_u32(frame + 10);
    return 0;
}

int fw_parse_downlink(const uint8_t *frame, int len, fw_downlink_t *out)
{
    uint8_t type;
    int payload_len;

    if (out == 0) return -1;
    if (len < FW_HDR_LEN + FW_SUM_LEN) return -1;
    if (frame[0] != FW_HEAD0 || frame[1] != FW_HEAD1) return -1;
    if (frame[2] != FW_DEV_HOST) return -1;

    type        = frame[3];
    payload_len = (int)fw_get_u16(frame + 8);
    if (fw_frame_expected_len(type, payload_len) != len) return -1;
    if (fw_frame_direction(FW_DEV_HOST, type) != FW_DIR_HOST_TO_DEV) return -1;
    if (frame[len - 1] != fw_checksum(frame, len - 1)) return -1;

    out->type = type;
    out->ts   = fw_get_u32(frame + 4);

    switch (type) {
    case FW_T_TIMESYNC:
    case FW_T_SYNCREQ:
        out->u.epoch = fw_get_u32(frame + 10);
        break;
    case FW_T_WIFI_CFG:
        out->u.ip[0] = frame[10];
        out->u.ip[1] = frame[11];
        out->u.ip[2] = frame[12];
        out->u.ip[3] = frame[13];
        break;
    case FW_T_CAPTURE:
        out->u.image_id = frame[10];
        break;
    case FW_T_MOTOR:
        out->u.motor.warn_type  = frame[10];
        out->u.motor.motor_mode = frame[11];
        break;
    default:
        return -1;
    }
    return 0;
}

/* ==================== 接收滑窗 ==================== */

void fw_rx_init(fw_rx_t *rx)
{
    rx->len = 0;
}

int fw_rx_feed(fw_rx_t *rx, const uint8_t *data, int data_len,
               uint8_t *out_frame, int out_cap)
{
    int i;

    for (i = 0; i < data_len; ++i) {
        uint8_t b = data[i];
        int frame_len;

        /* 同步头：未凑齐帧头时先对齐 0xAA 0x55 */
        if (rx->len == 0) {
            if (b != FW_HEAD0) continue;
        } else if (rx->len == 1) {
            if (b != FW_HEAD1) {
                rx->len = (b == FW_HEAD0) ? 1 : 0;
                continue;
            }
        }

        rx->buf[rx->len++] = b;

        if (rx->len < FW_HDR_LEN) continue;

        /* 凑齐 10 字节头即可算期望整帧长 */
        frame_len = fw_frame_expected_len(rx->buf[3], (int)fw_get_u16(rx->buf + 8));
        if (frame_len < 0 || frame_len > (int)sizeof(rx->buf)) {
            /* 非法长度：丢 1 字节重新同步（丢整段会连带丢掉后续好帧） */
            rx->len = (b == FW_HEAD0) ? 1 : 0;
            continue;
        }

        if (rx->len < frame_len) continue;   /* 等更多字节 */

        if (fw_verify_frame(rx->buf, frame_len) == 0) {
            if (out_cap < frame_len) {       /* 输出缓冲不够，整帧丢弃 */
                rx->len = 0;
                continue;
            }
            for (int k = 0; k < frame_len; ++k) out_frame[k] = rx->buf[k];
            rx->len = 0;
            return frame_len;                /* 一次返回一帧；剩余字节由调用者再喂 */
        }

        /* 校验失败：丢帧头第 1 字节，继续搜下一个同步头 */
        rx->len = (rx->buf[1] == FW_HEAD0) ? 1 : 0;
        if (rx->len == 1) rx->buf[0] = FW_HEAD0;
    }
    return 0;
}

/* ==================== 日志槽校验 ==================== */

int fw_slot_is_vital(const uint8_t *frame)
{
    return (fw_verify_frame(frame, FW_VITAL_FRAME) == 0) ? 1 : 0;
}

uint32_t fw_vital_ts(const uint8_t *frame)
{
    return fw_get_u32(frame + 4);
}
