#ifndef SCALE_TRANSPORT_H
#define SCALE_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

/* 体重秤的上行传输（发 0x02 体重帧）+ 下行接收（收 0x09）
 *
 * 为什么默认走 WiFi TCP 而不是协议原文的"通道 A"：
 *   协议 §3 把 0x02 归到通道 A（手环那条是 USB 串口）。但**秤没有多余的 USB 口**，
 *   而且 PC 侧的上位机是"手环一个串口 + 秤一个串口"。所以秤走网络更现实。
 *   ⚠️ 这需要上位机侧确认接收方式，见 docs/SCALE_TRANSPORT.md。
 *
 * 端口用 45679，与手环图像通道 45678 分开 —— 上位机的 WifiHandler 是单客户端，
 * 两个设备抢同一个端口必然有一方被拒。 */

/* 初始化并开始连接（非阻塞） */
void scale_transport_start(void);

/* 1 = 上行通道已建立 */
int  scale_transport_connected(void);

/* 发送一段字节。1 = 全部送出；0 = 未连接或发送失败 */
int  scale_transport_send(const uint8_t *data, uint32_t len);

/* 下发/刷新 PC 的 IPv4（来自 0x09）。地址变化时立即断开重连。幂等。 */
void scale_transport_apply_pc_ip(const uint8_t ip[4]);

/* 当前 PC IP（点分十进制），写入 out */
void scale_transport_pc_ip_str(char *out, size_t out_len);

/* 1 = WiFi 已连上 AP 且拿到 IP */
int  scale_transport_wifi_ready(void);

/* 统计 */
uint32_t scale_transport_stat_frames(void);     /* 已发送帧数 */
uint32_t scale_transport_stat_reconnects(void); /* 重连次数 */

#endif /* SCALE_TRANSPORT_H */
