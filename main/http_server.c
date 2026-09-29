#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "wifi_mgr.h"
#include "http_server.h"

static const char *TAG = "web";

/* 两个独立 httpd 实例：
 * - 控制 server（80）：状态页 / 抓拍，短请求，随叫随到。
 * - 流 server（81）：MJPEG 死循环独占其工作线程，单独放一个端口，
 *   避免推流时把控制页/抓拍卡死（esp_http_server 单实例仅一个工作线程）。 */
static httpd_handle_t s_ctrl_server = NULL;
static httpd_handle_t s_stream_server = NULL;

/* MJPEG 单客户端保护：fb_count=2，多路并发拉流会互抢帧缓冲，限一路。 */
static atomic_bool s_stream_busy = ATOMIC_VAR_INIT(false);

#define PART_BOUNDARY "123456789000000000000987654321"
#define STREAM_PORT   81

/* ---------------- MJPEG 流 ---------------- */

static esp_err_t stream_handler(httpd_req_t *req)
{
    /* 单客户端保护：已有一路在拉流则拒绝，避免抢帧缓冲。 */
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_stream_busy, &expected, true)) {
        ESP_LOGW(TAG, "已有客户端在拉流，拒绝新连接");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_hdr(req, "Retry-After", "3");
        httpd_resp_send(req, "stream busy", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=" PART_BOUNDARY);

    esp_err_t res = ESP_OK;
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGE(TAG, "取帧失败");
            res = ESP_FAIL;
            break;
        }

        char hdr[128];
        int hlen = snprintf(hdr, sizeof(hdr),
                            "--" PART_BOUNDARY "\r\n"
                            "Content-Type: image/jpeg\r\n"
                            "Content-Length: %u\r\n\r\n",
                            (unsigned)fb->len);
        res = httpd_resp_send_chunk(req, hdr, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }
        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            break; /* 客户端断开 */
        }
    }

    atomic_store(&s_stream_busy, false);
    return res == ESP_FAIL ? ESP_FAIL : ESP_OK;
}

/* ---------------- 单帧抓拍 ---------------- */

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "抓拍取帧失败");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

/* ---------------- 状态页 ---------------- */

static esp_err_t status_handler(httpd_req_t *req)
{
    char hostname[32] = "";
    char ip[16] = "";
    wifi_mgr_get_hostname(hostname, sizeof(hostname));
    wifi_mgr_get_sta_ip(ip, sizeof(ip));

    wifi_ap_record_t ap = {0};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }

    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    int64_t uptime = esp_timer_get_time() / 1000000;

    char html[1024];
    snprintf(html, sizeof(html),
             "<!DOCTYPE html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>ESP32-CAM</title></head>"
             "<body style='font-family:system-ui;padding:20px;max-width:520px;margin:0 auto'>"
             "<h1>%s</h1>"
             "<p>IP: %s</p>"
             "<p>WiFi: %s (%d dBm)</p>"
             "<p>Uptime: %lld s</p>"
             "<p>Free heap: %u B</p>"
             "<p>Free PSRAM: %u B</p>"
             "<p>视频流: <a href='http://%s:%d/stream'>:%d/stream</a></p>"
             "<p>抓拍: <a href='/capture'>/capture</a></p>"
             "</body></html>",
             hostname, ip, (char *)ap.ssid, rssi,
             (long long)uptime, (unsigned)free_heap, (unsigned)free_psram,
             ip, STREAM_PORT, STREAM_PORT);

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static const httpd_uri_t uri_status = {
    .uri = "/", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL
};
static const httpd_uri_t uri_capture = {
    .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL
};
static const httpd_uri_t uri_stream = {
    .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL
};

esp_err_t http_server_start(void)
{
    /* 控制 server（80）：状态页 + 抓拍 */
    httpd_config_t ctrl_cfg = HTTPD_DEFAULT_CONFIG();
    ctrl_cfg.max_uri_handlers = 4;
    ctrl_cfg.max_open_sockets = 4;

    if (httpd_start(&s_ctrl_server, &ctrl_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "控制 server 启动失败");
        return ESP_FAIL;
    }
    httpd_register_uri_handler(s_ctrl_server, &uri_status);
    httpd_register_uri_handler(s_ctrl_server, &uri_capture);

    /* 流 server（81）：MJPEG 独占，避免阻塞控制页 */
    httpd_config_t stream_cfg = HTTPD_DEFAULT_CONFIG();
    stream_cfg.server_port = STREAM_PORT;
    stream_cfg.ctrl_port += 1;            /* 内部控制端口须与控制 server 错开 */
    stream_cfg.max_uri_handlers = 1;
    stream_cfg.max_open_sockets = 2;

    if (httpd_start(&s_stream_server, &stream_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "流 server 启动失败");
        httpd_stop(s_ctrl_server);
        s_ctrl_server = NULL;
        return ESP_FAIL;
    }
    httpd_register_uri_handler(s_stream_server, &uri_stream);

    ESP_LOGI(TAG, "本地 Web 已启动 (状态页/抓拍 :80, MJPEG :%d/stream)", STREAM_PORT);
    return ESP_OK;
}
