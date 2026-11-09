#include "fw_wifi.h"
#include "app_config.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

static const char *TAG = "fw_wifi";

#define WIFI_GOT_IP_BIT   BIT0

/* ==================== 状态 ==================== */
static EventGroupHandle_t s_events;
static SemaphoreHandle_t  s_sock_lock;
static volatile int       s_wifi_ready;      /* 已连 AP 且拿到 IP */
static int                s_sock         = -1;
static volatile int       s_img_connected;
static volatile int       s_state_dirty;     /* 连接状态变化待上报 */
static uint8_t            s_pc_ip[4];        /* 目标 PC IPv4 */
static int                s_fail_count;
static volatile int       s_new_ip;          /* 收到 0x09 新地址 → 立即重试 */

/* ==================== 事件处理 ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA 启动，开始连接 AP");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "已关联 AP，等待 DHCP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_wifi_ready = 0;
            if (s_events) xEventGroupClearBits(s_events, WIFI_GOT_IP_BIT);
            ESP_LOGW(TAG, "AP 断开，2 秒后重连");
            /* 交给事件任务，避免在事件回调里直接阻塞重连 */
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_wifi_ready = 1;
        s_fail_count = 0;
        s_new_ip = 1;                 /* 拿到 IP → 立刻试图像通道 */
        ESP_LOGI(TAG, "拿到 IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        if (s_events) xEventGroupSetBits(s_events, WIFI_GOT_IP_BIT);
    }
}

/* ==================== 初始化 ==================== */
static void netif_and_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, NET_DEFAULT_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, NET_DEFAULT_PASSWORD, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));   /* 关省电，优先图像吞吐 */
    /* WiFi 必须在 STA_START 事件回调里 esp_wifi_connect()，只 start 不 connect 不会连 */
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ==================== TCP 图像通道 ==================== */
static void close_sock_locked(void)
{
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }
    if (s_img_connected) {
        s_img_connected = 0;
        s_state_dirty = 1;
    }
}

/* 带超时的 connect（select 可写即连接成功） */
static int connect_timeout(const char *ip, uint16_t port, int timeout_ms)
{
    struct sockaddr_in dest;
    struct timeval tv;
    fd_set wfds;
    int fd, flags, rc, err = 0;
    socklen_t elen = sizeof(err);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    rc = connect(fd, (struct sockaddr *)&dest, sizeof(dest));
    if (rc != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (rc <= 0) {                      /* 超时或出错 */
        close(fd);
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, flags);          /* 恢复阻塞 */
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

/* 退避间隔阶梯：1s → 2s → 5s → 10s（10s 封顶） */
static uint32_t backoff_ms_for(int fails)
{
    switch (fails) {
    case 0:  return NET_BACKOFF_MS_1;
    case 1:  return NET_BACKOFF_MS_2;
    case 2:  return NET_BACKOFF_MS_3;
    default: return NET_BACKOFF_MS_MAX;
    }
}

static void image_channel_task(void *arg)
{
    char ip[16];
    (void)arg;

    for (;;) {
        /* 等 WiFi 就绪 */
        if (!s_wifi_ready) {
            xEventGroupWaitBits(s_events, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE,
                                pdMS_TO_TICKS(1000));
            continue;
        }

        /* 已连接：观测对端 FIN/RST（图像通道无下行业务数据）；select 不持锁，避免阻塞发图 */
        if (s_img_connected) {
            fd_set rfds;
            struct timeval tv;
            int fd, r, n;
            char b;

            xSemaphoreTake(s_sock_lock, portMAX_DELAY);
            fd = s_sock;
            xSemaphoreGive(s_sock_lock);
            if (fd < 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            r = select(fd + 1, &rfds, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(fd, &rfds)) {
                n = recv(fd, &b, 1, MSG_DONTWAIT);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
                    if (s_sock == fd) {
                        ESP_LOGW(TAG, "图像通道对端已关闭，准备重连");
                        close_sock_locked();
                    }
                    xSemaphoreGive(s_sock_lock);
                }
            }
            continue;
        }

        fw_wifi_pc_ip_str(ip, sizeof(ip));

        xSemaphoreTake(s_sock_lock, portMAX_DELAY);
        if (s_sock < 0) {
            int fd = connect_timeout(ip, NET_IMAGE_TCP_PORT, 3000);
            if (fd >= 0) {
                s_sock = fd;
                s_img_connected = 1;
                s_state_dirty = 1;
                s_fail_count = 0;
                s_new_ip = 0;
                ESP_LOGI(TAG, "图像通道已连接 PC %s:%d", ip, NET_IMAGE_TCP_PORT);
            } else {
                s_new_ip = 0;
                ++s_fail_count;
                ESP_LOGW(TAG, "图像通道连接 %s:%d 失败（第 %d 次），%u ms 后重试",
                         ip, NET_IMAGE_TCP_PORT, s_fail_count,
                         (unsigned)backoff_ms_for(s_fail_count - 1));
            }
        }
        xSemaphoreGive(s_sock_lock);

        if (!s_img_connected) {
            uint32_t wait = backoff_ms_for(s_fail_count > 0 ? s_fail_count - 1 : 0);
            /* 退避期间收到新 0x09 → 立即重试，不等退避 */
            uint32_t step = 100;
            while (wait > 0) {
                vTaskDelay(pdMS_TO_TICKS(step > wait ? wait : step));
                wait -= (step > wait ? wait : step);
                if (s_new_ip) {
                    ESP_LOGI(TAG, "收到新的 PC IPv4，放弃退避立即重试");
                    break;
                }
            }
        }
    }
}

/* ==================== 对外接口 ==================== */

void fw_wifi_start(void)
{
    /* NVS 是 WiFi 驱动的依赖，调用方可能已经初始化过，这里只兜底 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    s_events    = xEventGroupCreate();
    s_sock_lock = xSemaphoreCreateMutex();
    s_sock      = -1;
    s_fail_count = 0;

    /* 默认目标地址（0x09 到达后会被覆盖） */
    {
        struct in_addr a;
        if (inet_pton(AF_INET, NET_DEFAULT_PC_IP, &a) == 1) {
            memcpy(s_pc_ip, &a.s_addr, 4);
        } else {
            s_pc_ip[0] = 192; s_pc_ip[1] = 168; s_pc_ip[2] = 1; s_pc_ip[3] = 100;
        }
    }

    netif_and_wifi_init();

    xTaskCreate(image_channel_task, "img_chan", 4096, NULL, 4, NULL);
}

int fw_wifi_is_ready(void)        { return s_wifi_ready ? 1 : 0; }
int fw_wifi_image_connected(void) { return s_img_connected ? 1 : 0; }

void fw_wifi_apply_pc_ip(const uint8_t ip[4])
{
    if (memcmp(s_pc_ip, ip, 4) == 0) {
        return;                     /* 幂等：地址没变不折腾 */
    }
    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    memcpy(s_pc_ip, ip, 4);
    s_new_ip = 1;
    /* IP 变了：断开现有连接，让任务用新地址重连 */
    close_sock_locked();
    xSemaphoreGive(s_sock_lock);

    ESP_LOGI(TAG, "0x09 刷新 PC IPv4 → %u.%u.%u.%u",
             ip[0], ip[1], ip[2], ip[3]);
}

void fw_wifi_pc_ip_str(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return;
    snprintf(out, out_len, "%u.%u.%u.%u",
             s_pc_ip[0], s_pc_ip[1], s_pc_ip[2], s_pc_ip[3]);
}

int fw_wifi_take_state_change(int *connected)
{
    if (!s_state_dirty) return 0;
    s_state_dirty = 0;
    if (connected) *connected = s_img_connected;
    return 1;
}

int fw_wifi_image_send(const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0;
    int ok = 1;

    if (data == NULL || len == 0) return 0;

    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    if (s_sock < 0 || !s_img_connected) {
        xSemaphoreGive(s_sock_lock);
        return 0;
    }
    while (sent < len) {
        int n = send(s_sock, data + sent, len - sent, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "图像通道发送失败（已送 %u/%u），转为重连",
                     (unsigned)sent, (unsigned)len);
            close_sock_locked();
            ++s_fail_count;
            ok = 0;
            break;
        }
        sent += (uint32_t)n;
    }
    xSemaphoreGive(s_sock_lock);
    return ok;
}
