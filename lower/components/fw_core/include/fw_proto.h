#ifndef FW_PROTO_H
#define FW_PROTO_H

#include <stdint.h>

/* 协议 v1.2 纯 C 实现（与上位机 communication/protocolparser.cpp 字节级一致）
 *
 * 帧 = AA 55 | dev(1) | type(1) | ts(4 BE) | len(2 BE) | payload(N) | sum(1)
 * sum = 字节 0..(len-1) 累加取低 8 位（帧头到载荷，不含 sum 自身）
 *
 * 通道划分（见 protocol_spec1.2.md §1 / §10）：
 *   通道 A：体征 / 控制 —— 字节流透传（本项目 ESP32-S3 用 UART0→PC 的 COM 口）
 *   通道 B：图像        —— WiFi TCP，PC 服务端、设备客户端，端口 45678
 *
 * v1.2 相对 v1.1 只做扩展，0x01~0x05 的帧格式与语义未改。 */

/* ========== 帧结构常量 ========== */
#define FW_HEAD0   0xAAu
#define FW_HEAD1   0x55u
#define FW_HDR_LEN 10
#define FW_SUM_LEN 1

/* ========== 设备类型 ========== */
#define FW_DEV_HOST  0x00u   /* 上位机（下行） */
#define FW_DEV_BAND  0x01u   /* 手环 */
#define FW_DEV_SCALE 0x02u   /* 体重秤 */
#define FW_DEV_IMAGE 0x03u   /* 图像源 */

/* ========== 帧类型 ========== */
#define FW_T_VITAL     0x01u   /* 体征         手环→PC  通道A */
#define FW_T_WEIGHT    0x02u   /* 体重         秤→PC    通道A */
#define FW_T_TIMESYNC  0x03u   /* 时间同步     PC→设备  通道A */
#define FW_T_BACKLOG   0x04u   /* 补传状态     手环→PC  通道A */
#define FW_T_SYNCREQ   0x05u   /* 同步请求     PC→手环  通道A */
#define FW_T_IMG_START 0x06u   /* 图像开始     图像源→PC 通道B */
#define FW_T_IMG_FRAG  0x07u   /* 图像分片     图像源→PC 通道B */
#define FW_T_IMG_END   0x08u   /* 图像结束     图像源→PC 通道B */
#define FW_T_WIFI_CFG  0x09u   /* WiFi配置下发 PC→设备  通道A */
#define FW_T_MOTOR     0x0Au   /* 震动指令     PC→设备  通道A（已分配，本版不实现） */
#define FW_T_CAPTURE   0x0Bu   /* 拍照指令     PC→设备  通道A */

/* ========== 载荷 / 整帧长度 ========== */
#define FW_VITAL_PAYLOAD  13
#define FW_VITAL_FRAME    24   /* 10 + 13 + 1 */
#define FW_BACKLOG_PAYLOAD 3
#define FW_BACKLOG_FRAME  14   /* 10 + 3 + 1  */
#define FW_WEIGHT_PAYLOAD  2
#define FW_WEIGHT_FRAME   13   /* 10 + 2 + 1  */
#define FW_HOST_PAYLOAD    4
#define FW_HOST_FRAME     15   /* 10 + 4 + 1；0x03/0x05/0x09/0x0A/0x0B 统一 */
#define FW_IMG_START_PAYLOAD 7
#define FW_IMG_START_FRAME  18 /* 10 + 7 + 1 */
#define FW_IMG_END_PAYLOAD   3
#define FW_IMG_END_FRAME    14 /* 10 + 3 + 1 */

/* 图像分片（协议常量 §11）——红线：载荷上限是 1027 而不是 1024，
 * 满片载荷 1027 若被按 1024 截断，末 3 字节会丢，整张图报废 */
#define FW_IMG_FRAG_DATA_MAX    1024
#define FW_IMG_FRAG_PAYLOAD_MAX 1027   /* 3 + 1024 */
#define FW_IMG_FRAG_FRAME_MAX   1038   /* 10 + 1027 + 1 */
#define FW_IMG_FRAG_HDR         3      /* 图ID1 + 序号2 */

/* 通道 B 常量 */
#define FW_IMAGE_TCP_PORT 45678u

/* 本版不分片图像的最长整帧（接收缓冲用） */
#define FW_IMG_FRAME_BUF_MAX FW_IMG_FRAG_FRAME_MAX

/* ========== 无效值哨兵（协议 §4） ========== */
#define FW_HR_INVALID     0xFFFFu
#define FW_BO_INVALID     0xFFu
#define FW_TEMP_INVALID   0x7FFFu
#define FW_HUM_INVALID    0xFFFFu
#define FW_SWEAT_INVALID  0xFFu
#define FW_CAL_INVALID    0xFFFFFFFFu
#define FW_WEIGHT_INVALID 0xFFFFu
#define FW_EPOCH_INVALID  0u          /* ts=0 表示设备时钟未校时 */

/* 图 ID 分配范围（PC 分配；设备在 0x06 中原样回填） */
#define FW_IMG_ID_MIN 0x01u
#define FW_IMG_ID_MAX 0x7Fu

/* ========== 帧类型合法性 ========== */
typedef enum {
    FW_DIR_INVALID = 0,
    FW_DIR_DEV_TO_HOST,   /* 设备 → 上位机 */
    FW_DIR_HOST_TO_DEV    /* 上位机 → 设备 */
} fw_frame_dir_t;

/* ========== 数据结构 ========== */
typedef struct {
    uint32_t ts;        /* 采集时刻 epoch 秒；0 = 未校时 */
    uint16_t hr;        /* BPM；0xFFFF 无效 */
    uint8_t  spo2;      /* %；0xFF 无效 */
    uint16_t temp_x10;  /* 0.1℃；0x7FFF 无效 */
    uint16_t hum_x10;   /* 0.1%RH；0xFFFF 无效 */
    uint8_t  sweat;     /* 0~5；0xFF 无效 */
    uint32_t cal;       /* 当日累计 kcal；0xFFFFFFFF 无效 */
    uint8_t  hr_state;  /* 0 正常 / 1 无效 / 2 降级 */
} fw_vital_t;

typedef struct {
    uint32_t ts;        /* 称重时刻 epoch 秒 */
    uint16_t weight_x100; /* 0.01kg；0xFFFF 无效 */
} fw_weight_t;

/* 上位机下行帧：0x03 校时 / 0x05 同步请求 */
typedef struct {
    uint8_t  type;      /* FW_T_TIMESYNC / FW_T_SYNCREQ */
    uint32_t ts;        /* 帧头时间戳（PC 发送时刻） */
    uint32_t epoch;     /* 0x03：校时目标；0x05：since（0 = 请求全部） */
} fw_host_t;

/* 0x09 WiFi 配置下发：载荷 = PC IPv4 大端 4 字节 */
typedef struct {
    uint8_t  ip[4];     /* 例 192.168.1.5 = C0 A8 01 05 */
} fw_wifi_cfg_t;

/* 0x0B 拍照指令：载荷 = 图ID1 + 保留3 */
typedef struct {
    uint8_t  image_id;  /* 0x01~0x7F */
} fw_capture_t;

/* 0x0A 震动指令：载荷 = 警告类型1 + 震动模式1 + 保留2（本版不实现，仅解析备用） */
typedef struct {
    uint8_t  warn_type; /* 0x01 心率异常 / 0x02 血氧偏低 */
    uint8_t  motor_mode;/* 0x01 短震 / 0x02 双震 / 0x03 长震 */
} fw_motor_cmd_t;

/* 任一完整下行帧的解析结果 */
typedef struct {
    uint8_t type;
    uint32_t ts;
    union {
        uint32_t      epoch;      /* 0x03 / 0x05 */
        uint8_t       ip[4];      /* 0x09 */
        uint8_t       image_id;   /* 0x0B */
        fw_motor_cmd_t motor;     /* 0x0A */
    } u;
} fw_downlink_t;

/* ========== 基础工具 ========== */
uint8_t  fw_checksum(const uint8_t *data, int len);
void     fw_put_u16(uint8_t *p, uint16_t v);
void     fw_put_u32(uint8_t *p, uint32_t v);
uint16_t fw_get_u16(const uint8_t *p);
uint32_t fw_get_u32(const uint8_t *p);

/* 帧合法性：方向与整帧长度；不合法返回 FW_DIR_INVALID */
fw_frame_dir_t fw_frame_direction(uint8_t dev, uint8_t type);
int  fw_frame_expected_len(uint8_t type, int payload_len);  /* 返回整帧长；-1 = 帧类型未知 */
int  fw_verify_frame(const uint8_t *frame, int len);        /* 0 = 合法（结构+校验） */

/* ========== 设备 → 上位机 组帧 ========== */
/* 所有 build 函数返回整帧长（写入 buf 的字节数），buf 需足够大 */
int fw_build_vital(uint8_t *buf, const fw_vital_t *v);                     /* 24B */
int fw_build_weight(uint8_t *buf, const fw_weight_t *w);                   /* 13B */
int fw_build_backlog(uint8_t *buf, int start, uint16_t count, uint32_t ts);/* 14B */

/* ========== 图像组帧（通道 B） ========== */
int fw_build_image_start(uint8_t *buf, uint8_t image_id, uint32_t total_bytes,
                         uint16_t fragment_count, uint32_t ts);            /* 18B */
int fw_build_image_fragment(uint8_t *buf, uint8_t image_id, uint16_t seq,
                            const uint8_t *data, uint16_t data_len, uint32_t ts);
int fw_build_image_end(uint8_t *buf, uint8_t image_id, uint16_t crc16,
                       uint32_t ts);                                       /* 14B；本版 crc16=0 */

/* 一张图拆成多少片 */
uint16_t fw_image_fragment_count(uint32_t total_bytes);

/* ========== 上位机 → 设备 解析 ========== */
/* 0x03 / 0x05（沿用 v1.1 语义，fw_proto.c 的 smoke 依赖此签名） */
int fw_parse_host(const uint8_t *frame, int len, fw_host_t *out);

/* v1.2 统一下行帧解析：0x03 / 0x05 / 0x09 / 0x0A / 0x0B */
int fw_parse_downlink(const uint8_t *frame, int len, fw_downlink_t *out);

/* 接收滑窗：喂字节，完整帧就绪时返回整帧长并回填 frame，否则返回 0。
 * 必须每个通道各持一个实例（A/B 共用会让半包互相穿插）。 */
typedef struct {
    uint8_t buf[FW_IMG_FRAG_FRAME_MAX];
    int     len;
} fw_rx_t;

void fw_rx_init(fw_rx_t *rx);
int  fw_rx_feed(fw_rx_t *rx, const uint8_t *data, int data_len,
                uint8_t *out_frame, int out_cap);

/* ========== 日志槽校验（供 fw_log 恢复用） ========== */
int      fw_slot_is_vital(const uint8_t *frame);   /* 1 = 该槽是合法体征帧 */
uint32_t fw_vital_ts(const uint8_t *frame);

#endif /* FW_PROTO_H */
