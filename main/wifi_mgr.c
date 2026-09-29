#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "app_config.h"
#include "nvs_store.h"
#include "wifi_mgr.h"

static const char *TAG = "wifi";

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static wifi_mgr_state_t s_state = WIFI_MGR_STATE_IDLE;
static esp_timer_handle_t s_reconnect_timer = NULL;
static uint32_t s_backoff_sec = 1;

static void set_state(wifi_mgr_state_t s)
{
    s_state = s;
    ESP_LOGI(TAG, "state -> %d", (int)s);
}

static void schedule_reconnect(void);

static void reconnect_timer_cb(void *arg)
{
    if (s_state == WIFI_MGR_STATE_CONNECTING) {
        ESP_LOGI(TAG, "重连中...");
        esp_wifi_connect();
    }
}

static void schedule_reconnect(void)
{
    if (s_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = reconnect_timer_cb,
            .name = "wifi_reconnect",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_reconnect_timer));
    }
    ESP_LOGW(TAG, "WiFi 断开，%" PRIu32 "s 后重试", s_backoff_sec);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_backoff_sec * 1000000ULL);

    uint32_t next = s_backoff_sec * 2;
    s_backoff_sec = (next > (uint32_t)CONFIG_APP_RECONNECT_BACKOFF_MAX_SEC)
                        ? (uint32_t)CONFIG_APP_RECONNECT_BACKOFF_MAX_SEC
                        : next;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == WIFI_MGR_STATE_CONNECTING || s_state == WIFI_MGR_STATE_CONNECTED) {
            set_state(WIFI_MGR_STATE_CONNECTING);
            schedule_reconnect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_backoff_sec = 1;
        set_state(WIFI_MGR_STATE_CONNECTED);
    }
}

static esp_err_t connect_sta(const char *ssid, const char *pass)
{
    esp_wifi_stop();

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (pass && pass[0] != '\0') {
        strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    set_state(WIFI_MGR_STATE_CONNECTING);
    return ESP_OK;
}

static esp_err_t start_ap(void)
{
    esp_wifi_stop();

    wifi_config_t cfg = {0};
    cfg.ap.channel = 1;
    cfg.ap.max_connection = APP_AP_MAX_STA_CONN;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    /* SSID = 前缀 + 网卡 MAC 后 3 字节，多台设备不重名 */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s%02X%02X%02X",
             APP_AP_SSID_PREFIX, (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    cfg.ap.ssid_len = strlen((char *)cfg.ap.ssid);
    strlcpy((char *)cfg.ap.password, CONFIG_APP_PROV_AP_PASSWORD, sizeof(cfg.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    set_state(WIFI_MGR_STATE_PROVISIONING);
    ESP_LOGI(TAG, "配网热点: %s / 密码 %s", (char *)cfg.ap.ssid, CONFIG_APP_PROV_AP_PASSWORD);
    return ESP_OK;
}

esp_err_t wifi_mgr_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* 凭据自管在 NVS，WiFi 驱动用 RAM 存校准，减少闪存写 */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_store_has_wifi()) {
        ESP_ERROR_CHECK(nvs_store_wifi_get(ssid, sizeof(ssid), pass, sizeof(pass)));
        ESP_LOGI(TAG, "发现已存凭据，连接 %s", ssid);
        return connect_sta(ssid, pass);
    }
    ESP_LOGI(TAG, "无已存凭据，进入配网模式");
    return wifi_mgr_enter_provisioning();
}

esp_err_t wifi_mgr_enter_provisioning(void)
{
    return start_ap();
}

esp_err_t wifi_mgr_apply_credentials(const char *ssid, const char *pass)
{
    ESP_ERROR_CHECK(nvs_store_wifi_set(ssid, pass));
    ESP_LOGI(TAG, "凭据已保存，连接 %s", ssid);
    return connect_sta(ssid, pass);
}

esp_err_t wifi_mgr_stop_provisioning(void)
{
    esp_wifi_stop();
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_store_has_wifi()) {
        ESP_ERROR_CHECK(nvs_store_wifi_get(ssid, sizeof(ssid), pass, sizeof(pass)));
        return connect_sta(ssid, pass);
    }
    set_state(WIFI_MGR_STATE_IDLE);
    ESP_LOGW(TAG, "配网超时，进入待机（复位可重试）");
    return ESP_OK;
}

wifi_mgr_state_t wifi_mgr_get_state(void)
{
    return s_state;
}

esp_err_t wifi_mgr_get_ap_ip(char *buf, size_t len)
{
    if (s_ap_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_ap_netif, &ip) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_ip4addr_ntoa(&ip.ip, buf, len);
    return ESP_OK;
}

esp_err_t wifi_mgr_get_hostname(char *buf, size_t len)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return ESP_FAIL;
    }
    snprintf(buf, len, "%s-%02X%02X%02X", APP_HOSTNAME_PREFIX,
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    return ESP_OK;
}

esp_err_t wifi_mgr_get_sta_ip(char *buf, size_t len)
{
    if (s_sta_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_sta_netif, &ip) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_ip4addr_ntoa(&ip.ip, buf, len);
    return ESP_OK;
}
