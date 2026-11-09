#include "scale_transport.h"
#include "scale_config.h"
#include "fw_proto.h"

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

static const char *TAG = "scale_net";

#define WIFI_GOT_IP_BIT BIT0

static EventGroupHandle_t s_events;
static SemaphoreHandle_t  s_lock;
static volatile int       s_wifi_ready;
static int                s_sock = -1;
static volatile int       s_connected;
static uint8_t            s_pc_ip[4];
static volatile int       s_new_ip;
static int                s_fail_count;
static volatile uint32_t  s_stat_frames;
static volatile uint32_t  s_stat_reconnects;

/* ==================== WiFi ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "STA 启动，连接 AP");
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_wifi_ready = 0;
            if (s_events) xEventGroupClearBits(s_events, WIFI_GOT_IP_BIT);
            ESP_LOGW(TAG, "AP 断开，2 秒后重连");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_wifi_ready = 1;
        s_fail_count = 0;
        s_new_ip = 1;
        ESP_LOGI(TAG, "拿到 IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        if (s_events) xEventGroupSetBits(s_events, WIFI_GOT_IP_BIT);
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    {
        wifi_config_t wc;
        memset(&wc, 0, sizeof(wc));
        strncpy((char *)wc.sta.ssid, SCALE_DEFAULT_SSID, sizeof(wc.sta.ssid) - 1);
        strncpy((char *)wc.sta.password, SCALE_DEFAULT_PASSWORD, sizeof(wc.sta.password) - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
        /* WiFi 必须在 STA_START 事件里 esp_wifi_connect()，只 start 不 connect 不会连 */
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

/* ==================== TCP ==================== */
static void close_sock_locked(void)
{
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }
    s_connected = 0;
}

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

    if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
        close(fd);
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, flags);
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

static uint32_t backoff_ms_for(int fails)
{
    switch (fails) {
    case 0:  return SCALE_BACKOFF_MS_1;
    case 1:  return SCALE_BACKOFF_MS_2;
    case 2:  return SCALE_BACKOFF_MS_3;
    default: return SCALE_BACKOFF_MS_MAX;
    }
}

/* 处理下行帧：秤只关心 0x09（PC 地址下发）。
 * 用 15 字节滑窗，因为下行帧都是定长（协议 §11 红线 2）。 */
static void handle_downlink(fw_rx_t *rx, const uint8_t *data, int len)
{
    uint8_t frame[FW_IMG_FRAG_FRAME_MAX];
    int off = 0;

    while (off < len) {
        int n = fw_rx_feed(rx, data + off, len - off, frame, sizeof(frame));
        if (n <= 0) break;              /* 没凑齐一帧，剩余字节留在 rx 里等下一批 */

        /* fw_rx_feed 返回"整帧长度"。其中可能有 k 字节是上一批残留在 rx 里的，
         * 所以它本次实际消费的是 (n - k) 字节，k 不可知 —— 保守取 0，
         * 即把 off 推进 n 字节。最坏情况下会重复解析极少量字节，无副作用
         * （0x09 幂等，重复施加同一个 IP 不会重连）。 */
        off += n;

        if (fw_verify_frame(frame, n) != 0) continue;
        if (frame[2] != FW_DEV_HOST) continue;

        if (frame[3] == FW_T_WIFI_CFG && n == FW_HOST_FRAME) {
            scale_transport_apply_pc_ip(frame + 10);
        }
    }
}

static void transport_task(void *arg)
{
    static fw_rx_t rx;      /* 下行解析滑窗（static：栈上放不下 1038B 也无需） */
    char ip[16];
    (void)arg;

    fw_rx_init(&rx);

    for (;;) {
        if (!s_wifi_ready) {
            xEventGroupWaitBits(s_events, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE,
                                pdMS_TO_TICKS(1000));
            continue;
        }

        scale_transport_pc_ip_str(ip, sizeof(ip));

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_sock < 0) {
            int fd = connect_timeout(ip, SCALE_TCP_PORT, 3000);
            if (fd >= 0) {
                s_sock = fd;
                s_connected = 1;
                s_fail_count = 0;
                s_new_ip = 0;
                ++s_stat_reconnects;
                ESP_LOGI(TAG, "已连接 PC %s:%d", ip, SCALE_TCP_PORT);
            } else {
                s_new_ip = 0;
                ++s_fail_count;
                ESP_LOGW(TAG, "连接 %s:%d 失败（第 %d 次），%u ms 后重试",
                         ip, SCALE_TCP_PORT, s_fail_count,
                         (unsigned)backoff_ms_for(s_fail_count - 1));
            }
        }
        xSemaphoreGive(s_lock);

        if (s_connected) {
            /* 读下行（0x09 可能在连接后随时到达） */
            uint8_t buf[128];
            struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
            fd_set rfds;
            int fd;

            xSemaphoreTake(s_lock, portMAX_DELAY);
            fd = s_sock;
            xSemaphoreGive(s_lock);

            if (fd < 0) continue;

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0) {
                int r = recv(fd, buf, sizeof(buf), 0);
                if (r > 0) {
                    handle_downlink(&rx, buf, r);
                } else if (r == 0) {
                    ESP_LOGW(TAG, "PC 关闭了连接");
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    close_sock_locked();
                    xSemaphoreGive(s_lock);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGW(TAG, "接收失败 (%d)，转为重连", errno);
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    close_sock_locked();
                    xSemaphoreGive(s_lock);
                }
            }
        } else {
            uint32_t wait = backoff_ms_for(s_fail_count > 0 ? s_fail_count - 1 : 0);
            while (wait > 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                wait -= 100;
                if (s_new_ip) {
                    ESP_LOGI(TAG, "收到新的 PC IPv4，放弃退避立即重试");
                    break;
                }
            }
        }
    }
}

/* ==================== 对外 ==================== */
void scale_transport_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    s_events = xEventGroupCreate();
    s_lock   = xSemaphoreCreateMutex();
    s_sock   = -1;

    {
        struct in_addr a;
        if (inet_pton(AF_INET, SCALE_DEFAULT_PC_IP, &a) == 1) {
            memcpy(s_pc_ip, &a.s_addr, 4);
        } else {
            s_pc_ip[0] = 192; s_pc_ip[1] = 168; s_pc_ip[2] = 1; s_pc_ip[3] = 100;
        }
    }

    wifi_init();
    xTaskCreate(transport_task, "scale_net", 4096, NULL, 5, NULL);
}

int scale_transport_connected(void)  { return s_connected ? 1 : 0; }
int scale_transport_wifi_ready(void) { return s_wifi_ready ? 1 : 0; }

int scale_transport_send(const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0;
    int ok = 1;

    if (data == NULL || len == 0) return 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sock < 0 || !s_connected) {
        xSemaphoreGive(s_lock);
        return 0;
    }
    while (sent < len) {
        int n = send(s_sock, data + sent, len - sent, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "发送失败（已送 %u/%u），转为重连", (unsigned)sent, (unsigned)len);
            close_sock_locked();
            ++s_fail_count;
            ok = 0;
            break;
        }
        sent += (uint32_t)n;
    }
    if (ok) ++s_stat_frames;
    xSemaphoreGive(s_lock);
    return ok;
}

void scale_transport_apply_pc_ip(const uint8_t ip[4])
{
    if (memcmp(s_pc_ip, ip, 4) == 0) return;    /* 幂等 */

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_pc_ip, ip, 4);
    s_new_ip = 1;
    close_sock_locked();
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "0x09 刷新 PC IPv4 → %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void scale_transport_pc_ip_str(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return;
    snprintf(out, out_len, "%u.%u.%u.%u", s_pc_ip[0], s_pc_ip[1], s_pc_ip[2], s_pc_ip[3]);
}

uint32_t scale_transport_stat_frames(void)     { return s_stat_frames; }
uint32_t scale_transport_stat_reconnects(void) { return s_stat_reconnects; }
