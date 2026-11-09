#include "band_sensors.h"
#include "app_config.h"
#include "max30102_algo.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensors";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_max30102;
static i2c_master_dev_handle_t s_sht30;

/* ============================================================
 * MAX30102 寄存器
 * ============================================================ */
#define MAX30102_REG_INTR_STATUS_1  0x00
#define MAX30102_REG_INTR_STATUS_2  0x01
#define MAX30102_REG_INTR_ENABLE_1  0x02
#define MAX30102_REG_INTR_ENABLE_2  0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C
#define MAX30102_REG_LED2_PA        0x0D
#define MAX30102_REG_PILOT_PA       0x10

#define MAX30102_I2C_TIMEOUT_MS     100

/* 100sps → 每 10ms 一个样本 */
#define MAX30102_SAMPLE_PERIOD_MS   10
#define MAX30102_FIFO_DEPTH         32

/* 采样缓冲（Maxim 算法要求 500 样本 = 5 秒 @100sps） */
static uint32_t s_ir_buf[MAX30102_ALGO_BUFFER_SIZE];
static uint32_t s_red_buf[MAX30102_ALGO_BUFFER_SIZE];
static int      s_sample_count;

/* 最新有效结果 */
static volatile uint16_t s_hr   = FW_HR_INVALID;
static volatile uint8_t  s_spo2 = FW_BO_INVALID;
static volatile int      s_hr_valid;
static volatile int      s_spo2_valid;

/* 最新温湿度 */
static volatile uint16_t s_temp_x10 = FW_TEMP_INVALID;
static volatile uint16_t s_hum_x10  = FW_HUM_INVALID;

/* ============================================================
 * I2C 基础
 * ============================================================ */
static esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), MAX30102_I2C_TIMEOUT_MS);
}

static esp_err_t i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg,
                               uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, len, MAX30102_I2C_TIMEOUT_MS);
}

/* ============================================================
 * MAX30102
 * ============================================================ */
static void max30102_clear_fifo(void)
{
    i2c_write_reg(s_max30102, MAX30102_REG_FIFO_WR_PTR, 0x00);
    i2c_write_reg(s_max30102, MAX30102_REG_OVF_COUNTER,  0x00);
    i2c_write_reg(s_max30102, MAX30102_REG_FIFO_RD_PTR,  0x00);
}

static void max30102_reset(void)
{
    i2c_write_reg(s_max30102, MAX30102_REG_MODE_CONFIG, 0x40);   /* RESET */
    vTaskDelay(pdMS_TO_TICKS(20));
    max30102_clear_fifo();
}

static void max30102_configure(void)
{
    /* 配置依据 datum\接线说明.md「寄存器配置 (商家验证)」 */
    i2c_write_reg(s_max30102, MAX30102_REG_INTR_ENABLE_1, 0xC0);  /* FIFO 几乎满 + 数据就绪 */
    i2c_write_reg(s_max30102, MAX30102_REG_INTR_ENABLE_2, 0x00);
    max30102_clear_fifo();
    i2c_write_reg(s_max30102, MAX30102_REG_FIFO_CONFIG,   0x0F);  /* 平均1 / 不翻滚 / 几乎满=17 */
    i2c_write_reg(s_max30102, MAX30102_REG_MODE_CONFIG,   0x03);  /* SpO2 模式（红光+红外）*/
    i2c_write_reg(s_max30102, MAX30102_REG_SPO2_CONFIG,   0x27);  /* 4096nA / 100Hz / 400us */
    i2c_write_reg(s_max30102, MAX30102_REG_LED1_PA,       0x24);  /* 红光 ~7mA */
    i2c_write_reg(s_max30102, MAX30102_REG_LED2_PA,       0x24);  /* 红外 ~7mA */
    i2c_write_reg(s_max30102, MAX30102_REG_PILOT_PA,      0x7F);  /* 近距离检测 ~25mA */
}

/* FIFO 里还有几个未读样本 */
static int max30102_fifo_available(void)
{
    uint8_t wr = 0, rd = 0;
    if (i2c_read_regs(s_max30102, MAX30102_REG_FIFO_WR_PTR, &wr, 1) != ESP_OK) return -1;
    if (i2c_read_regs(s_max30102, MAX30102_REG_FIFO_RD_PTR, &rd, 1) != ESP_OK) return -1;
    return ((int)wr - (int)rd + MAX30102_FIFO_DEPTH) % MAX30102_FIFO_DEPTH;
}

/* 一次读干 FIFO：返回本批读到的样本数（每个样本 6B = 红光3 + 红外3） */
static int max30102_drain_fifo(uint32_t *reds, uint32_t *irs, int max_samples)
{
    uint8_t raw[MAX30102_FIFO_DEPTH * 6];
    int avail = max30102_fifo_available();
    int n, i;

    if (avail <= 0) return 0;
    if (avail > max_samples) avail = max_samples;
    n = avail;

    if (i2c_read_regs(s_max30102, MAX30102_REG_FIFO_DATA, raw, (size_t)(n * 6)) != ESP_OK) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        const uint8_t *p = raw + i * 6;
        reds[i] = (((uint32_t)(p[0] & 0x03)) << 16) | ((uint32_t)p[1] << 8) | p[2];
        irs[i]  = (((uint32_t)(p[3] & 0x03)) << 16) | ((uint32_t)p[4] << 8) | p[5];
    }
    return n;
}

/* 10ms 采样任务：只负责喂满算法缓冲并更新结果，不做任何阻塞式 IO */
static void max30102_task(void *arg)
{
    uint32_t reds[MAX30102_FIFO_DEPTH];
    uint32_t irs[MAX30102_FIFO_DEPTH];
    TickType_t last = xTaskGetTickCount();
    uint32_t log_div = 0;

    (void)arg;

    for (;;) {
        int n = max30102_drain_fifo(reds, irs, MAX30102_FIFO_DEPTH);
        int i;

        /* 溢出检测：积压超过 FIFO 深度一半说明采样跟不上，清一次防止永久错位 */
        if (n > MAX30102_FIFO_DEPTH / 2) {
            ESP_LOGW(TAG, "MAX30102 FIFO 积压 %d 个样本，清空重同步", n);
            max30102_clear_fifo();
            n = 0;
        }

        for (i = 0; i < n; ++i) {
            if (s_sample_count < MAX30102_ALGO_BUFFER_SIZE) {
                s_ir_buf[s_sample_count]  = irs[i];
                s_red_buf[s_sample_count] = reds[i];
                ++s_sample_count;
            } else {
                /* 滑动窗口：丢最旧 100，补 100 新（与商家算法一致） */
                memmove(s_red_buf, s_red_buf + 100,
                        (MAX30102_ALGO_BUFFER_SIZE - 100) * sizeof(uint32_t));
                memmove(s_ir_buf, s_ir_buf + 100,
                        (MAX30102_ALGO_BUFFER_SIZE - 100) * sizeof(uint32_t));
                s_sample_count = MAX30102_ALGO_BUFFER_SIZE - 100;
                s_ir_buf[s_sample_count]  = irs[i];
                s_red_buf[s_sample_count] = reds[i];
                ++s_sample_count;
            }
        }

        /* 缓冲已满 → 跑算法 */
        if (s_sample_count >= MAX30102_ALGO_BUFFER_SIZE) {
            int32_t spo2 = 0, hr = 0;
            int8_t  spo2_valid = 0, hr_valid = 0;

            maxim_heart_rate_and_oxygen_saturation(
                s_ir_buf, MAX30102_ALGO_BUFFER_SIZE, s_red_buf,
                &spo2, &spo2_valid, &hr, &hr_valid);

            if (hr_valid && hr > 20 && hr < 250) {
                s_hr = (uint16_t)hr;
                s_hr_valid = 1;
            } else {
                s_hr = FW_HR_INVALID;
                s_hr_valid = 0;
            }
            if (spo2_valid && spo2 > 50 && spo2 <= 100) {
                s_spo2 = (uint8_t)spo2;
                s_spo2_valid = 1;
            } else {
                s_spo2 = FW_BO_INVALID;
                s_spo2_valid = 0;
            }

            if (++log_div >= 100) {   /* 每 1 秒打一次，别刷屏 */
                log_div = 0;
                ESP_LOGI(TAG, "HR=%d(valid=%d) SpO2=%d(valid=%d) 样本=%d",
                         (int)s_hr, (int)s_hr_valid, (int)s_spo2,
                         (int)s_spo2_valid, s_sample_count);
            }
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(MAX30102_SAMPLE_PERIOD_MS));
    }
}

/* ============================================================
 * SHT30
 * ============================================================ */
/* CRC-8：多项式 0x31，初值 0xFF（SHT3x 数据手册） */
static uint8_t sht30_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    int i, b;
    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        for (b = 0; b < 8; ++b) {
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1));
        }
    }
    return crc;
}

/* 单次测量：返回 1 成功。优先 0x24 0x00（无时钟拉伸），失败再退 0x2C 0x06 */
static int sht30_measure(uint16_t *temp_x10, uint16_t *hum_x10)
{
    static const uint8_t cmds[2][2] = {
        { 0x24, 0x00 },   /* 单次，高重复性，无时钟拉伸 */
        { 0x2C, 0x06 },   /* 单次，高重复性，时钟拉伸（兜底） */
    };
    int c;

    for (c = 0; c < 2; ++c) {
        uint8_t data[6];
        esp_err_t err;

        err = i2c_master_transmit(s_sht30, cmds[c], 2, MAX30102_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20));   /* 高重复性最大 15ms，留 20ms 余量 */

        /* 用 transmit_receive：需要重复起始条件，单独 receive 部分器件不响应 */
        err = i2c_master_receive(s_sht30, data, sizeof(data), MAX30102_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            continue;
        }

        /* CRC 校验：不过就换下一种读法 */
        if (sht30_crc8(&data[0], 2) != data[2] || sht30_crc8(&data[3], 2) != data[5]) {
            ESP_LOGW(TAG, "SHT30 CRC 校验失败（cmd 0x%02X）：%02X %02X %02X %02X %02X %02X",
                     cmds[c][0], data[0], data[1], data[2], data[3], data[4], data[5]);
            continue;
        }

        {
            uint16_t raw_t = (uint16_t)((data[0] << 8) | data[1]);
            uint16_t raw_h = (uint16_t)((data[3] << 8) | data[4]);
            int32_t  t     = -450 + ((int32_t)raw_t * 1750) / 65535;   /* 0.1℃ */
            uint32_t h     = ((uint32_t)raw_h * 1000u) / 65535u;       /* 0.1%RH */

            if (t < -400 || t > 1250 || h > 1000) {
                continue;   /* 明显不合理，换下一种读法 */
            }
            *temp_x10 = (uint16_t)t;
            *hum_x10  = (uint16_t)h;
            return 1;
        }
    }
    return 0;
}

/* 温湿度任务：1 秒一次足够（SHT30 是环境量，不需要高频） */
static void sht30_task(void *arg)
{
    int first_fail_logged = 0;
    (void)arg;

    for (;;) {
        uint16_t t, h;
        if (sht30_measure(&t, &h)) {
            s_temp_x10 = t;
            s_hum_x10  = h;
            first_fail_logged = 0;
        } else if (!first_fail_logged) {
            ESP_LOGE(TAG, "SHT30 读取失败（地址 0x%02X）—— 检查 I2C 接线与上拉电阻",
                     SHT30_ADDR);
            first_fail_logged = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ============================================================
 * 马达
 * ============================================================ */
static int     s_motor_active;
static int64_t s_motor_end_us;

void band_motor_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << MOTOR_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(MOTOR_PIN, 0);
}

void band_motor_on(uint16_t duration_ms)
{
    s_motor_active = 1;
    s_motor_end_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    gpio_set_level(MOTOR_PIN, 1);
}

void band_motor_tick(void)
{
    if (s_motor_active && esp_timer_get_time() > s_motor_end_us) {
        s_motor_active = 0;
        gpio_set_level(MOTOR_PIN, 0);
    }
}

/* ============================================================
 * I2C 扫描 / 初始化
 * ============================================================ */
int band_sensors_i2c_scan(void)
{
    int found = 0;
    uint8_t addr;

    for (addr = 1; addr < 127; ++addr) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C 设备在线: 0x%02X%s", addr,
                     (addr == MAX30102_ADDR) ? " (MAX30102)" :
                     (addr == SHT30_ADDR)    ? " (SHT30)" : "");
            ++found;
        }
    }
    ESP_LOGI(TAG, "I2C 扫描结束，共 %d 个设备", found);
    return found;
}

void band_sensors_init(void)
{
    /* ---- I2C 总线 ---- */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT_NUM,
        .sda_io_num        = I2C_PIN_SDA,
        .scl_io_num        = I2C_PIN_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    /* ---- 设备 ---- */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MAX30102_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_max30102) != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 句柄创建失败");
    }
    dev_cfg.device_address = SHT30_ADDR;
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_sht30) != ESP_OK) {
        ESP_LOGE(TAG, "SHT30 句柄创建失败");
    }

    /* ---- 先扫描，接线问题一眼可见 ---- */
    band_sensors_i2c_scan();

    /* ---- MAX30102 复位 + 配置 ---- */
    max30102_reset();
    max30102_configure();

    /* ---- SHT30 软复位 ---- */
    {
        uint8_t soft_reset[2] = { 0x30, 0xA2 };
        i2c_master_transmit(s_sht30, soft_reset, 2, MAX30102_I2C_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* ---- 马达 ---- */
    band_motor_init();

    /* ---- 任务 ---- */
    xTaskCreate(max30102_task, "max30102", 4096, NULL, 6, NULL);
    xTaskCreate(sht30_task,    "sht30",    3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "传感器初始化完成（MAX30102 @%dms / SHT30 @1000ms）",
             MAX30102_SAMPLE_PERIOD_MS);
}

/* ============================================================
 * 对外读数
 * ============================================================ */
void band_sensors_read(fw_vital_t *out)
{
    if (out == NULL) return;

    out->hr      = s_hr;
    out->spo2    = s_spo2;
    out->temp_x10 = s_temp_x10;
    out->hum_x10  = s_hum_x10;
    out->sweat    = FW_SWEAT_INVALID;
    out->cal      = FW_CAL_INVALID;
    /* 0 正常 / 1 无效 / 2 降级：算法说无效就是 1；
     * 缓冲还没满（前 5 秒）也按无效报，避免上位机显示 0 */
    out->hr_state = (s_hr_valid && s_spo2_valid) ? 0
                  : (s_sample_count < MAX30102_ALGO_BUFFER_SIZE ? 1 : 2);
}
