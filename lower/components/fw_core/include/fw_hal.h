#ifndef FW_HAL_H
#define FW_HAL_H

#include <stdint.h>

/* 固件核心（fw_core）唯一允许依赖的板级接口。
 * PC 端高位机冒烟测试用 firmware/sim 的模拟实现；
 * 上板时由 main 实现这三个结构体（SPI Flash / UART / 软时钟）——核心代码零改动。 */

typedef struct {
    void *ctx;
    /* 存储介质（SPI Flash / RAM / SD 均可） */
    int      (*read)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
    int      (*program)(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len); /* 1→0 编程 */
    int      (*erase_sector)(void *ctx, uint32_t sector_index);                      /* 擦为 0xFF */
    uint32_t size_bytes;    /* 总容量 */
    uint32_t sector_size;   /* 擦除块大小；0 = RAM 模式（无擦除概念） */
} fw_storage_t;

typedef struct {
    void *ctx;
    void (*send)(void *ctx, const uint8_t *buf, uint32_t len);   /* USART/蓝牙发送 */
} fw_uart_t;

typedef struct {
    void *ctx;
    uint32_t (*epoch_now)(void *ctx);                  /* 软时钟：epoch 秒 */
    void     (*set_epoch)(void *ctx, uint32_t epoch);  /* 0x03 校时 */
    uint32_t (*millis)(void *ctx);                     /* 单调毫秒（调度用） */
} fw_clock_t;

#endif /* FW_HAL_H */
