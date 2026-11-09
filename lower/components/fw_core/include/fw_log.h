#ifndef FW_LOG_H
#define FW_LOG_H

#include <stdint.h>
#include "fw_hal.h"
#include "fw_proto.h"

/* 离线缓存环形日志：槽 = [序号 4B][整帧 24B] = 28 字节
 *
 * 设计要点（都�?firmware_smoke 里被验证）：
 *  1) 序号用于上电恢复：扫描找"最大序号的有效�?= 最新帧，再按序号逐个 -1 向旧回走�?
 *     走到断点即窗口边界（跨过上一轮未擦除的旧数据会因序号断层自动停住）；
 *  2) 擦除纪律：进入新扇区前先整扇区擦除（该扇区保存的正是窗口内最旧的若干帧）�?
 *     扇区内被提前丢弃的帧会同步从窗口里扣�?—�?保证"窗口内每一帧都真在介质�?�?
 *  3) 写入顺序：先写序号、再写帧体。掉电最坏丢 1 帧，不会把旧数据误认成新帧；
 *  4) 容量代价�?8/24 �?+17% 存储开销�?MB �?�?3.5 天，16MB �?�?7 天）�?
 *
 * FW-A2 变更：对外序号统一�?绝对帧序�?�? 起单调递增，不随环绕回绕）�?
 * 供补传按序号逐帧取帧�?追尾"判断；新�?fw_log_get_seq / fw_log_total�?*/

typedef struct {
    fw_storage_t st;
    uint32_t slots_per_sector;   /* 0 = RAM 模式 */
    uint32_t slot_count;
    uint32_t total;              /* 已写入帧总数�? 下一帧的绝对序号�?*/
    uint32_t oldest;             /* 窗口内最旧帧的绝对序�?*/
} fw_log_t;

int      fw_log_init(fw_log_t *log, const fw_storage_t *st);  /* 上电扫描恢复 */
int      fw_log_append(fw_log_t *log, const uint8_t *frame24);
uint32_t fw_log_count(const fw_log_t *log);      /* 窗口内帧�?*/
uint32_t fw_log_capacity(const fw_log_t *log);   /* 槽位总数 */
uint32_t fw_log_total(const fw_log_t *log);      /* 下一帧绝对序号（补传"追尾"依据�?*/
int      fw_log_get(const fw_log_t *log, uint32_t i, uint8_t *frame24);       /* i=0 为窗口内最�?*/
int      fw_log_get_seq(const fw_log_t *log, uint32_t seq, uint8_t *frame24); /* 按绝对序号取�?*/
/* 检索第一�?ts > since 的帧：返�?1 并回填绝对序号与条数；返�?0 = 无（含空日志）�?
 * since=0 表示"全部"。前提：窗口内帧时间戳单调不减（软时钟只前进/�?0x03 校时�?*/
int      fw_log_find_since(const fw_log_t *log, uint32_t since_ts,
                           uint32_t *first_seq, uint32_t *count);

#endif /* FW_LOG_H */
