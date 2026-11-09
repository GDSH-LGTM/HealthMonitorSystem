#ifndef FW_CAMERA_H
#define FW_CAMERA_H

#include <stdint.h>

/* ATK-OV2640 摄像头（DVP + SCCB）
 *
 * 硬件要点：模块板载 12MHz 有源晶振（Y1 直连 XVCLK，排针无 XCLK），
 * 故 pin_xclk=-1、ESP32 不输出时钟；但 xclk_freq_hz 必须填 12MHz
 * （CAM_XCLK_HZ），填 0 会让驱动 160000000/xclk 除零 panic。
 *
 * 帧缓冲落地：
 *   有 PSRAM → CAMERA_FB_IN_PSRAM，fb_count=2（本板模组型号未确认，故运行时判断）
 *   无 PSRAM → CAMERA_FB_IN_DRAM，fb_count=1（QVGA JPEG 约 8~15KB，DRAM 放得下）
 *
 * 注意：ESP32-S3 只有一路摄像头接口，摄像头与 LCD 的引脚在本项目里不冲突，
 * 但摄像头占用 GPIO4-14/16/17/18/47，接线较多。 */

typedef struct {
    int      pin_d0, pin_d1, pin_d2, pin_d3, pin_d4, pin_d5, pin_d6, pin_d7;
    int      pin_vsync, pin_href, pin_pclk, pin_xclk;   /* XCLK 填 -1 */
    int      pin_sccb_sda, pin_sccb_scl;
    int      pin_pwdn, pin_reset;
    uint16_t frame_width;    /* 期望输出宽度（仅用于日志） */
    uint16_t frame_height;
    int      jpeg_quality;   /* 0~63，越小质量越高 */
} fw_camera_cfg_t;

/* 初始化摄像头。成功返回 0；失败返回 -1（不阻塞启动） */
int  fw_camera_init(const fw_camera_cfg_t *cfg);

/* 1 = 摄像头可用 */
int  fw_camera_is_ready(void);

/* 采集一帧 JPEG。成功返回 0，并回填：
 *   data/len —— 指向 JPEG 数据的只读指针与长度
 *   out_frame —— 不透明帧句柄，用完必须原样交给 fw_camera_return_frame()
 * ⚠️ 不归还帧缓冲会耗尽缓冲池，后续采集会失败。 */
int  fw_camera_capture(const uint8_t **data, uint32_t *len, void **out_frame);

/* 归还帧缓冲。frame 必须是 fw_camera_capture 回填的那个句柄 */
void fw_camera_return_frame(void *frame);

/* 取最近一次错误描述（可为 NULL） */
const char *fw_camera_last_error(void);

#endif /* FW_CAMERA_H */
