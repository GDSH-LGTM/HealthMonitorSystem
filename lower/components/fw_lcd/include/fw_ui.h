#ifndef FW_UI_H
#define FW_UI_H

#include <stdint.h>
#include "fw_lcd.h"
#include "fw_touch.h"

/* LCD 像素尺寸（与 app_config.h 的 LCD_W/LCD_H 保持一致；此处独立定义避免组件依赖配置头） */
#define FW_UI_WIDTH  240
#define FW_UI_HEIGHT 320

/* LVGL 8 界面：显示心率/血氧/温湿度 + 两条通道的连接状态
 *
 * 本模块只负责显示与触摸，不读传感器——数据由 main 通过 fw_ui_update_vitals 推入，
 * 这样 fw_lcd 不必依赖 fw_sensors（避免组件循环依赖）。 */

/* 初始化 LVGL：显示驱动 + 触摸输入 + 建界面。lcd/touch 必须已 init。 */
int  fw_ui_init(void);

/* 主循环调用（内部自己算节拍，不需要外部定时器） */
void fw_ui_loop(void);

/* 更新体征显示。有效值传 -1 / -1.0f 表示无效（显示 "--"） */
void fw_ui_update_vitals(int hr, int spo2, float temp_c, float hum_pct, int hr_state);

/* 通道状态：wifi_ready = 连上热点；img_connected = 图像 TCP 已连；pc_ip 可为 NULL */
void fw_ui_update_status(int wifi_ready, int img_connected, const char *pc_ip);

/* 启动期进度提示（在 LVGL 起来之前用不了，故用 LCD 直接填色 + 提示由日志负责） */
void fw_ui_boot_screen(void);

#endif /* FW_UI_H */
