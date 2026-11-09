/* Maxim 官方 MAX30102 心率/血氧算法 — 移植自 Maxim AN5300
 * 纯C实现, 无平台依赖, 可直接在 ESP32-S3 上运行
 * 算法: 4点移动平均 → 差分 → 汉明窗 → 峰值检测 → 心率
 *       AC/DC比值 → 查表 → 血氧
 */
#include "max30102_algo.h"

/* 汉明窗系数 (512*hamming(5)) */
static const uint16_t auw_hamm[MAX30102_ALGO_HAMMING_SIZE] = {41, 276, 512, 276, 41};

/* 血氧查表 (-45.060*r^2 + 30.354*r + 94.845) */
static const uint8_t uch_spo2_table[184] = {
    95,95,95,96,96,96,97,97,97,97,98,98,98,98,98,99,99,99,99,
    99,99,99,99,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,
    100,100,100,100,99,99,99,99,99,99,99,99,98,98,98,98,98,98,97,97,
    97,97,96,96,96,96,95,95,95,94,94,94,93,93,93,92,92,92,91,91,
    90,90,89,89,89,88,88,87,87,86,86,85,85,84,84,83,82,82,81,81,
    80,80,79,78,78,77,76,76,75,74,74,73,72,72,71,70,69,69,68,67,
    66,66,65,64,63,62,62,61,60,59,58,57,56,56,55,54,53,52,51,50,
    49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,31,30,29,
    28,27,26,25,23,22,21,20,19,17,16,15,14,12,11,10,9,7,6,5,
    3,2,1
};

static int32_t an_dx[MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_MA4_SIZE];
static int32_t an_x[MAX30102_ALGO_BUFFER_SIZE];
static int32_t an_y[MAX30102_ALGO_BUFFER_SIZE];

/* ---- 辅助排序函数 ---- */
static void maxim_sort_ascend(int32_t *pn_x, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_x[i];
        for (j = i; j > 0 && n_temp < pn_x[j-1]; j--)
            pn_x[j] = pn_x[j-1];
        pn_x[j] = n_temp;
    }
}

static void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_indx[i];
        for (j = i; j > 0 && pn_x[n_temp] > pn_x[pn_indx[j-1]]; j--)
            pn_indx[j] = pn_indx[j-1];
        pn_indx[j] = n_temp;
    }
}

static void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks,
    int32_t *pn_x, int32_t n_size, int32_t n_min_height)
{
    int32_t i = 1, n_width;
    *pn_npks = 0;
    while (i < n_size - 1) {
        if (pn_x[i] > n_min_height && pn_x[i] > pn_x[i-1]) {
            n_width = 1;
            while (i + n_width < n_size && pn_x[i] == pn_x[i + n_width])
                n_width++;
            if (pn_x[i] > pn_x[i + n_width] && (*pn_npks) < 15) {
                pn_locs[(*pn_npks)++] = i;
                i += n_width + 1;
            } else {
                i += n_width;
            }
        } else {
            i++;
        }
    }
}

static void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks,
    int32_t *pn_x, int32_t n_min_distance)
{
    int32_t i, j, n_old_npks, n_dist;
    maxim_sort_indices_descend(pn_x, pn_locs, *pn_npks);
    for (i = -1; i < *pn_npks; i++) {
        n_old_npks = *pn_npks;
        *pn_npks = i + 1;
        for (j = i + 1; j < n_old_npks; j++) {
            n_dist = pn_locs[j] - (i == -1 ? -1 : pn_locs[i]);
            if (n_dist > n_min_distance || n_dist < -n_min_distance)
                pn_locs[(*pn_npks)++] = pn_locs[j];
        }
    }
    maxim_sort_ascend(pn_locs, *pn_npks);
}

static void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks,
    int32_t *pn_x, int32_t n_size, int32_t n_min_height,
    int32_t n_min_distance, int32_t n_max_num)
{
    maxim_peaks_above_min_height(pn_locs, pn_npks, pn_x, n_size, n_min_height);
    maxim_remove_close_peaks(pn_locs, pn_npks, pn_x, n_min_distance);
    *pn_npks = min(*pn_npks, n_max_num);
}

/* ---- 主算法 ---- */
void maxim_heart_rate_and_oxygen_saturation(
    uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
    uint32_t *pun_red_buffer,
    int32_t *pn_spo2, int8_t *pch_spo2_valid,
    int32_t *pn_heart_rate, int8_t *pch_hr_valid)
{
    uint32_t un_ir_mean, un_only_once;
    int32_t k, n_i_ratio_count;
    int32_t i, s, m, n_exact_ir_valley_locs_count, n_middle_idx;
    int32_t n_th1, n_npks, n_c_min;
    int32_t an_ir_valley_locs[15];
    int32_t an_exact_ir_valley_locs[15];
    int32_t an_dx_peak_locs[15];
    int32_t n_peak_interval_sum;
    int32_t n_y_ac, n_x_ac;
    int32_t n_spo2_calc;
    int32_t n_y_dc_max, n_x_dc_max;
    int32_t n_y_dc_max_idx = 0, n_x_dc_max_idx = 0;
    int32_t an_ratio[5], n_ratio_average;
    int32_t n_nume, n_denom;

    /* 去直流 */
    un_ir_mean = 0;
    for (k = 0; k < n_ir_buffer_length; k++)
        un_ir_mean += pun_ir_buffer[k];
    un_ir_mean = un_ir_mean / n_ir_buffer_length;
    for (k = 0; k < n_ir_buffer_length; k++)
        an_x[k] = pun_ir_buffer[k] - un_ir_mean;

    /* 4点移动平均 */
    for (k = 0; k < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_MA4_SIZE; k++) {
        n_denom = (an_x[k] + an_x[k+1] + an_x[k+2] + an_x[k+3]);
        an_x[k] = n_denom / (int32_t)4;
    }

    /* 差分 */
    for (k = 0; k < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_MA4_SIZE - 1; k++)
        an_dx[k] = (an_x[k+1] - an_x[k]);

    /* 2点移动平均 */
    for (k = 0; k < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_MA4_SIZE - 2; k++)
        an_dx[k] = (an_dx[k] + an_dx[k+1]) / 2;

    /* 汉明窗 (翻转波形以检测谷值) */
    for (i = 0; i < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_HAMMING_SIZE - MAX30102_ALGO_MA4_SIZE - 2; i++) {
        s = 0;
        for (k = i; k < i + MAX30102_ALGO_HAMMING_SIZE; k++)
            s -= an_dx[k] * auw_hamm[k - i];
        an_dx[i] = s / (int32_t)1146;
    }

    /* 阈值计算 */
    n_th1 = 0;
    for (k = 0; k < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_HAMMING_SIZE; k++)
        n_th1 += ((an_dx[k] > 0) ? an_dx[k] : ((int32_t)0 - an_dx[k]));
    n_th1 = n_th1 / (MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_HAMMING_SIZE);

    /* 峰值检测 */
    maxim_find_peaks(an_dx_peak_locs, &n_npks, an_dx,
        MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_HAMMING_SIZE, n_th1, 8, 5);

    /* 心率 = 60000 / (峰间距 * 10ms) */
    n_peak_interval_sum = 0;
    if (n_npks >= 2) {
        for (k = 1; k < n_npks; k++)
            n_peak_interval_sum += (an_dx_peak_locs[k] - an_dx_peak_locs[k-1]);
        n_peak_interval_sum = n_peak_interval_sum / (n_npks - 1);
        *pn_heart_rate = (int32_t)(6000 / n_peak_interval_sum);
        *pch_hr_valid = 1;
    } else {
        *pn_heart_rate = -999;
        *pch_hr_valid = 0;
    }

    for (k = 0; k < n_npks; k++)
        an_ir_valley_locs[k] = an_dx_peak_locs[k] + MAX30102_ALGO_HAMMING_SIZE / 2;

    /* 恢复原始值: RED=y, IR=x */
    for (k = 0; k < n_ir_buffer_length; k++) {
        an_x[k] = pun_ir_buffer[k];
        an_y[k] = pun_red_buffer[k];
    }

    /* 精确定位谷值 */
    n_exact_ir_valley_locs_count = 0;
    for (k = 0; k < n_npks; k++) {
        un_only_once = 1;
        m = an_ir_valley_locs[k];
        n_c_min = 16777216;
        if (m + 5 < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_HAMMING_SIZE && m - 5 > 0) {
            for (i = m - 5; i < m + 5; i++) {
                if (an_x[i] < n_c_min) {
                    if (un_only_once > 0) un_only_once = 0;
                    n_c_min = an_x[i];
                    an_exact_ir_valley_locs[k] = i;
                }
            }
            if (un_only_once == 0)
                n_exact_ir_valley_locs_count++;
        }
    }

    if (n_exact_ir_valley_locs_count < 2) {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
        return;
    }

    /* 4点移动平均 */
    for (k = 0; k < MAX30102_ALGO_BUFFER_SIZE - MAX30102_ALGO_MA4_SIZE; k++) {
        an_x[k] = (an_x[k] + an_x[k+1] + an_x[k+2] + an_x[k+3]) / (int32_t)4;
        an_y[k] = (an_y[k] + an_y[k+1] + an_y[k+2] + an_y[k+3]) / (int32_t)4;
    }

    /* AC/DC 比值 → 查表得血氧 */
    n_ratio_average = 0;
    n_i_ratio_count = 0;
    for (k = 0; k < 5; k++) an_ratio[k] = 0;

    for (k = 0; k < n_exact_ir_valley_locs_count; k++) {
        if (an_exact_ir_valley_locs[k] > MAX30102_ALGO_BUFFER_SIZE) {
            *pn_spo2 = -999;
            *pch_spo2_valid = 0;
            return;
        }
    }

    for (k = 0; k < n_exact_ir_valley_locs_count - 1; k++) {
        n_y_dc_max = -16777216;
        n_x_dc_max = -16777216;
        if (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k] > 10) {
            for (i = an_exact_ir_valley_locs[k]; i < an_exact_ir_valley_locs[k+1]; i++) {
                if (an_x[i] > n_x_dc_max) { n_x_dc_max = an_x[i]; n_x_dc_max_idx = i; }
                if (an_y[i] > n_y_dc_max) { n_y_dc_max = an_y[i]; n_y_dc_max_idx = i; }
            }
            n_y_ac = (an_y[an_exact_ir_valley_locs[k+1]] - an_y[an_exact_ir_valley_locs[k]]) *
                     (n_y_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[an_exact_ir_valley_locs[k]] +
                     n_y_ac / (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[n_y_dc_max_idx] - n_y_ac;

            n_x_ac = (an_x[an_exact_ir_valley_locs[k+1]] - an_x[an_exact_ir_valley_locs[k]]) *
                     (n_x_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[an_exact_ir_valley_locs[k]] +
                     n_x_ac / (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[n_y_dc_max_idx] - n_x_ac;

            n_nume = (n_y_ac * n_x_dc_max) >> 7;
            n_denom = (n_x_ac * n_y_dc_max) >> 7;
            if (n_denom > 0 && n_i_ratio_count < 5 && n_nume != 0) {
                an_ratio[n_i_ratio_count] = (n_nume * 20) / n_denom;
                n_i_ratio_count++;
            }
        }
    }

    maxim_sort_ascend(an_ratio, n_i_ratio_count);
    n_middle_idx = n_i_ratio_count / 2;

    if (n_middle_idx > 1)
        n_ratio_average = (an_ratio[n_middle_idx - 1] + an_ratio[n_middle_idx]) / 2;
    else
        n_ratio_average = an_ratio[n_middle_idx];

    if (n_ratio_average > 2 && n_ratio_average < 184) {
        n_spo2_calc = uch_spo2_table[n_ratio_average];
        *pn_spo2 = n_spo2_calc;
        *pch_spo2_valid = 1;
    } else {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
    }
}
