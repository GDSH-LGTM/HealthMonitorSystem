#ifndef FW_WIFI_H
#define FW_WIFI_H

#include <stdint.h>
#include <stddef.h>

/* WiFi 图像通道（协议 §10）
 *
 * 角色：PC = TCP 服务端（端口 45678 固定）；设备 = TCP 客户端
 * 地址来源：优先 0x09 下发的 PC IPv4；未收到时用 config.h 的兜底值
 * 重连：连接失败/断开 → 1s → 2s → 5s → 10s 退避（10s 封顶）
 *       收到新 0x09（fw_wifi_apply_pc_ip）→ 立即以新地址重试，不等退避
 * 通道解耦：本通道的连接状态不参与"设备在线"判定（那个只看通道 A 数据帧）
 *
 * 注意：SSID / 密码不经协议传输，由设备侧预置（NVS 可覆盖）。 */

/* 打开 WiFi STA 并开始连接（非阻塞，返回后自行在后台连上） */
void fw_wifi_start(void);

/* 1 = WiFi 已连上 AP 且拿到 IP */
int fw_wifi_is_ready(void);

/* 1 = 与 PC 的图像 TCP 通道已建立 */
int fw_wifi_image_connected(void);

/* 下发/刷新 PC 的 IPv4（来自 0x09）。可在任意任务上下文调用。 */
void fw_wifi_apply_pc_ip(const uint8_t ip[4]);

/* 当前使用的 PC IPv4（点分十进制字符串），写入 out（至少 16 字节） */
void fw_wifi_pc_ip_str(char *out, size_t out_len);

/* 连接状态变化标记：返回 1 表示自上次调用后发生过变化（读后清零）。
 * 供主循环打印日志 / 刷新 LVGL 状态。 */
int fw_wifi_take_state_change(int *connected);

/* 向图像通道发送一段字节。1 = 全部送出；0 = 未连接或发送失败 */
int fw_wifi_image_send(const uint8_t *data, uint32_t len);

#endif /* FW_WIFI_H */
