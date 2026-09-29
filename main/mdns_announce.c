#include "mdns.h"
#include "esp_log.h"
#include "wifi_mgr.h"
#include "mdns_announce.h"

static const char *TAG = "mdns";

esp_err_t mdns_announce_init(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    char hostname[32];
    if (wifi_mgr_get_hostname(hostname, sizeof(hostname)) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-CAM Home Camera"));
    /* 广告 HTTP 服务（状态页 + MJPEG 均在 80 端口） */
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));

    ESP_LOGI(TAG, "mDNS 已启动: %s.local", hostname);
    return ESP_OK;
}
