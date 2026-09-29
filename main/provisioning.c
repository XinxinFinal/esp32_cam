#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "app_config.h"
#include "nvs_store.h"
#include "wifi_mgr.h"
#include "provisioning.h"

static const char *TAG = "prov";

/* ---------------- 配网页（注意：CSS 里的字面 % 要写成 %%） ---------------- */
static const char HTML_FORM[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>ESP32-CAM 配网</title>"
    "<style>"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#f4f5f7;margin:0;padding:20px;color:#222}"
    ".card{max-width:420px;margin:0 auto;background:#fff;border-radius:12px;padding:24px;box-shadow:0 2px 12px rgba(0,0,0,.08)}"
    "h1{font-size:20px;margin:0 0 4px}"
    "p.sub{color:#888;font-size:13px;margin:0 0 20px}"
    "h2{font-size:12px;color:#999;border-top:1px solid #eee;padding-top:16px;margin:22px 0 0;text-transform:uppercase;letter-spacing:.5px}"
    "label{display:block;font-size:13px;margin:14px 0 4px;color:#555}"
    "input{width:100%%;box-sizing:border-box;padding:10px;border:1px solid #ddd;border-radius:8px;font-size:15px}"
    "input[type=submit]{background:#2f7cf6;color:#fff;border:none;margin-top:22px;font-size:16px;padding:12px;font-weight:600}"
    "input[type=submit]:active{background:#2563d6}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>ESP32-CAM 配网</h1>"
    "<p class='sub'>连接家庭 WiFi 与 MQTT（Home Assistant）</p>"
    "<form method='post' action='/'>"
    "<h2>WiFi</h2>"
    "<label>SSID</label>"
    "<input name='ssid' required maxlength='32' value='%s'>"
    "<label>密码</label>"
    "<input name='pass' type='password' maxlength='64' placeholder='开放网络留空'>"
    "<h2>MQTT</h2>"
    "<label>服务器地址</label>"
    "<input name='mqtt_host' maxlength='64' value='%s' placeholder='如 192.168.1.10'>"
    "<label>端口</label>"
    "<input name='mqtt_port' type='number' value='%u' min='1' max='65535'>"
    "<label>用户名（可选）</label>"
    "<input name='mqtt_user' maxlength='32' value='%s'>"
    "<label>密码（可选）</label>"
    "<input name='mqtt_pass' type='password' maxlength='32'>"
    "<label>主题前缀</label>"
    "<input name='mqtt_prefix' maxlength='32' value='%s'>"
    "<input type='submit' value='保存并连接'>"
    "</form></div></body></html>";

static const char HTML_OK_TEMPLATE[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'></head>"
    "<body style='font-family:system-ui;padding:30px;text-align:center'>"
    "<h1>正在连接…</h1>"
    "<p>设备将连接到 <b>%s</b> 并关闭本热点。</p>"
    "<p>稍后请访问 <a href='http://esp32cam-xxxx.local/'>esp32cam-xxxx.local</a></p>"
    "</body></html>";

/* ---------------- 小工具 ---------------- */

static void html_escape(const char *src, char *dst, size_t dst_len)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < dst_len; i++) {
        switch (src[i]) {
        case '&': memcpy(dst + j, "&amp;", 5);  j += 5; break;
        case '<': memcpy(dst + j, "&lt;", 4);   j += 4; break;
        case '>': memcpy(dst + j, "&gt;", 4);   j += 4; break;
        case '\'': memcpy(dst + j, "&#39;", 5); j += 5; break;
        case '"': memcpy(dst + j, "&quot;", 6); j += 6; break;
        default: dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void urldecode(const char *src, size_t len, char *out, size_t out_len)
{
    size_t i = 0, j = 0;
    while (i < len && j + 1 < out_len) {
        char c = src[i];
        if (c == '+') {
            out[j++] = ' ';
            i++;
        } else if (c == '%' && i + 2 < len) {
            int hi = hexval(src[i + 1]);
            int lo = hexval(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 3;
            } else {
                out[j++] = c;
                i++;
            }
        } else {
            out[j++] = c;
            i++;
        }
    }
    out[j] = '\0';
}

/* 从 URL 编码的表单体里取某个键的值 */
static void form_get(const char *body, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            const char *end = strchr(p, '&');
            size_t vlen = end ? (size_t)(end - p) : strlen(p);
            urldecode(p, vlen, out, out_len);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

/* ---------------- HTTP 处理 ---------------- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char ssid[33] = "";
    char host[64] = "";
    uint16_t port = APP_MQTT_DEFAULT_PORT;
    char user[33] = "";
    char prefix[32] = "";
    strlcpy(prefix, APP_MQTT_DEFAULT_TOPIC_PREFIX, sizeof(prefix));

    char pass_dummy[65];
    if (nvs_store_has_wifi()) {
        nvs_store_wifi_get(ssid, sizeof(ssid), pass_dummy, sizeof(pass_dummy));
    }
    char mq_pass_dummy[33];
    if (nvs_store_mqtt_get(host, sizeof(host), &port, user, sizeof(user),
                           mq_pass_dummy, sizeof(mq_pass_dummy), prefix, sizeof(prefix)) != ESP_OK) {
        strlcpy(prefix, APP_MQTT_DEFAULT_TOPIC_PREFIX, sizeof(prefix));
    }

    char e_ssid[128], e_host[192], e_user[128], e_prefix[128];
    html_escape(ssid, e_ssid, sizeof(e_ssid));
    html_escape(host, e_host, sizeof(e_host));
    html_escape(user, e_user, sizeof(e_user));
    html_escape(prefix, e_prefix, sizeof(e_prefix));

    size_t need = strlen(HTML_FORM) + strlen(e_ssid) + strlen(e_host) +
                  strlen(e_user) + strlen(e_prefix) + 64;
    char *html = malloc(need);
    if (html == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    snprintf(html, need, HTML_FORM, e_ssid, e_host, (unsigned)port, e_user, e_prefix);

    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req, html, strlen(html));
    free(html);
    return err;
}

static char s_pending_ssid[33] = "";
static char s_pending_pass[65] = "";

static void apply_after_delay(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_mgr_apply_credentials(s_pending_ssid, s_pending_pass);
    vTaskDelete(NULL);
}

static esp_err_t root_post_handler(httpd_req_t *req)
{
    char body[1024];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty request");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char ssid[33] = "", pass[65] = "";
    char host[64] = "", port_str[8] = "", user[33] = "", mpass[33] = "", prefix[32] = "";
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "pass", pass, sizeof(pass));
    form_get(body, "mqtt_host", host, sizeof(host));
    form_get(body, "mqtt_port", port_str, sizeof(port_str));
    form_get(body, "mqtt_user", user, sizeof(user));
    form_get(body, "mqtt_pass", mpass, sizeof(mpass));
    form_get(body, "mqtt_prefix", prefix, sizeof(prefix));

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    /* MQTT：host 非空才保存，避免误清已有配置 */
    if (host[0] != '\0') {
        uint16_t port = (uint16_t)atoi(port_str[0] ? port_str : "1883");
        if (prefix[0] == '\0') {
            strlcpy(prefix, APP_MQTT_DEFAULT_TOPIC_PREFIX, sizeof(prefix));
        }
        nvs_store_mqtt_set(host, port, user, mpass, prefix);
    }

    char e_ssid[128];
    html_escape(ssid, e_ssid, sizeof(e_ssid));
    char ok[512];
    snprintf(ok, sizeof(ok), HTML_OK_TEMPLATE, e_ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ok, strlen(ok));

    /* 延迟切 STA，确保响应先被客户端收到 */
    strlcpy(s_pending_ssid, ssid, sizeof(s_pending_ssid));
    strlcpy(s_pending_pass, pass, sizeof(s_pending_pass));
    xTaskCreate(apply_after_delay, "apply", 3072, NULL, 5, NULL);

    return ESP_OK;
}

/* Captive Portal：所有其它路径 302 到 / */
static esp_err_t redirect_to_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_root_get = {
    .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL
};
static const httpd_uri_t uri_root_post = {
    .uri = "/", .method = HTTP_POST, .handler = root_post_handler, .user_ctx = NULL
};
static const httpd_uri_t uri_wildcard = {
    .uri = "/*", .method = HTTP_GET, .handler = redirect_to_root, .user_ctx = NULL
};

static httpd_handle_t s_server = NULL;

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.uri_match_fn = httpd_uri_match_wildcard; /* 启用通配符 URI 匹配 */

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败");
        return;
    }
    /* 精确匹配须先注册，通配符放最后 */
    httpd_register_uri_handler(s_server, &uri_root_get);
    httpd_register_uri_handler(s_server, &uri_root_post);
    httpd_register_uri_handler(s_server, &uri_wildcard);
    ESP_LOGI(TAG, "配网页已上线: http://192.168.4.1/");
}

static void stop_http_server(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

/* ---------------- Captive Portal DNS 劫持 ---------------- */

static volatile bool s_dns_stop = false;

/* 把 A 查询应答成 AP 的 IP */
static int build_dns_response(const uint8_t *q, int qlen, uint8_t *r)
{
    if (qlen < 12) {
        return -1;
    }
    memcpy(r, q, 12);
    r[2] = 0x81; /* QR=1, opcode=0, RD=1 */
    r[3] = 0x80; /* no error */
    uint16_t qd = (q[4] << 8) | q[5];
    if (qd != 1) {
        return -1;
    }

    int pos = 12;
    while (pos < qlen && q[pos] != 0) {
        int lab = q[pos];
        if (lab >= 64 || pos + 1 + lab >= qlen) {
            return -1;
        }
        pos += 1 + lab;
    }
    if (pos >= qlen || q[pos] != 0 || pos + 5 > qlen) {
        return -1;
    }
    pos++;
    uint16_t qtype = (q[pos] << 8) | q[pos + 1];
    int qend = pos + 4; /* 名字 + qtype + qclass */

    memcpy(r + 12, q + 12, qend - 12);
    int rlen = qend;

    uint16_t an = 0;
    if (qtype == 0x0001 /* A */ || qtype == 0x00FF /* ANY */) {
        uint8_t ip4[4] = {192, 168, 4, 1};
        char ip[16];
        if (wifi_mgr_get_ap_ip(ip, sizeof(ip)) == ESP_OK) {
            int a, b, c, d;
            if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                ip4[0] = (uint8_t)a; ip4[1] = (uint8_t)b;
                ip4[2] = (uint8_t)c; ip4[3] = (uint8_t)d;
            }
        }
        r[rlen++] = 0xC0; r[rlen++] = 0x0C; /* 指向 offset 12 的域名 */
        r[rlen++] = 0x00; r[rlen++] = 0x01; /* TYPE A */
        r[rlen++] = 0x00; r[rlen++] = 0x01; /* CLASS IN */
        r[rlen++] = 0x00; r[rlen++] = 0x00; r[rlen++] = 0x00; r[rlen++] = 0x3C; /* TTL 60 */
        r[rlen++] = 0x00; r[rlen++] = 0x04; /* RDLENGTH 4 */
        r[rlen++] = ip4[0]; r[rlen++] = ip4[1]; r[rlen++] = ip4[2]; r[rlen++] = ip4[3];
        an = 1;
    }

    r[6] = (an >> 8) & 0xFF;
    r[7] = an & 0xFF;
    r[8] = 0; r[9] = 0;    /* NSCOUNT */
    r[10] = 0; r[11] = 0;  /* ARCOUNT */
    return rlen;
}

static void dns_task(void *arg)
{
    /* 稍等 AP IP 就绪 */
    vTaskDelay(pdMS_TO_TICKS(800));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    int fl = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);

    uint8_t rx[512], tx[512];
    while (!s_dns_stop) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&client, &clen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            break;
        }
        int rlen = build_dns_response(rx, n, tx);
        if (rlen > 0) {
            sendto(sock, tx, rlen, 0, (struct sockaddr *)&client, clen);
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

static void start_dns(void)
{
    s_dns_stop = false;
    xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);
}

static void stop_dns(void)
{
    s_dns_stop = true;
}

/* ---------------- 生命周期 ---------------- */

static void provisioning_task(void *arg)
{
    /* 等 AP IP 就绪（最多 5s） */
    char ap_ip[16];
    int waited = 0;
    while (wifi_mgr_get_ap_ip(ap_ip, sizeof(ap_ip)) != ESP_OK && waited < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited++;
    }

    start_dns();
    start_http_server();

    int64_t start_us = esp_timer_get_time();
    int64_t timeout_us = (int64_t)CONFIG_APP_PROV_AP_TIMEOUT_SEC * 1000000LL;
    bool timed_out = false;

    while (wifi_mgr_get_state() == WIFI_MGR_STATE_PROVISIONING) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (esp_timer_get_time() - start_us > timeout_us) {
            timed_out = true;
            break;
        }
    }

    stop_http_server();
    stop_dns();

    if (timed_out && wifi_mgr_get_state() == WIFI_MGR_STATE_PROVISIONING) {
        ESP_LOGW(TAG, "配网超时，关闭热点");
        wifi_mgr_stop_provisioning();
    }
    vTaskDelete(NULL);
}

esp_err_t provisioning_start(void)
{
    if (xTaskCreate(provisioning_task, "prov", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
