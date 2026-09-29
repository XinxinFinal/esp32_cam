#include <stdio.h>
#include <string.h>
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
static httpd_handle_t s_server = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"

/* ---------------- MJPEG 流 ---------------- */

static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=" PART_BOUNDARY);

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGE(TAG, "取帧失败");
            return ESP_FAIL;
        }

        char hdr[128];
        int hlen = snprintf(hdr, sizeof(hdr),
                            "--" PART_BOUNDARY "\r\n"
                            "Content-Type: image/jpeg\r\n"
                            "Content-Length: %u\r\n\r\n",
                            (unsigned)fb->len);
        esp_err_t res = httpd_resp_send_chunk(req, hdr, hlen);
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
    return ESP_OK;
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
             "<p>视频流: <a href='/stream'>/stream</a></p>"
             "</body></html>",
             hostname, ip, (char *)ap.ssid, rssi,
             (long long)uptime, (unsigned)free_heap, (unsigned)free_psram);

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static const httpd_uri_t uri_status = {
    .uri = "/", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL
};
static const httpd_uri_t uri_stream = {
    .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL
};

esp_err_t http_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败");
        return ESP_FAIL;
    }
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_stream);

    ESP_LOGI(TAG, "本地 Web 已启动 (状态页 /, MJPEG /stream)");
    return ESP_OK;
}
