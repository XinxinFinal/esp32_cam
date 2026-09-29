#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "nvs_store.h"

static const char *TAG = "nvs";

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区损坏，擦除重建");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* ---------------- WiFi 凭据 ---------------- */

bool nvs_store_has_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, NVS_KEY_WIFI_SSID, NULL, &len);
    nvs_close(h);
    return (err == ESP_OK && len > 0);
}

esp_err_t nvs_store_wifi_get(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, NVS_KEY_WIFI_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    err = nvs_get_str(h, NVS_KEY_WIFI_PASS, pass, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0'; /* 开放网络 */
    } else if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_store_wifi_set(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_WIFI_SSID, ssid);
    if (err == ESP_OK && pass != NULL) {
        err = nvs_set_str(h, NVS_KEY_WIFI_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t nvs_store_wifi_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---------------- MQTT 配置 ---------------- */

esp_err_t nvs_store_mqtt_get(char *host, size_t host_len, uint16_t *port,
                             char *user, size_t user_len, char *pass, size_t pass_len,
                             char *prefix, size_t prefix_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_MQTT, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, NVS_KEY_MQTT_HOST, host, &host_len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    uint32_t p = APP_MQTT_DEFAULT_PORT;
    nvs_get_u32(h, NVS_KEY_MQTT_PORT, &p);
    *port = (uint16_t)p;

    if (nvs_get_str(h, NVS_KEY_MQTT_USER, user, &user_len) != ESP_OK) {
        user[0] = '\0';
    }
    if (nvs_get_str(h, NVS_KEY_MQTT_PASS, pass, &pass_len) != ESP_OK) {
        pass[0] = '\0';
    }
    if (nvs_get_str(h, NVS_KEY_MQTT_PREFIX, prefix, &prefix_len) != ESP_OK) {
        strlcpy(prefix, APP_MQTT_DEFAULT_TOPIC_PREFIX, prefix_len);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_store_mqtt_set(const char *host, uint16_t port,
                             const char *user, const char *pass, const char *prefix)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_MQTT, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_MQTT_HOST, host);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, NVS_KEY_MQTT_PORT, port);
    }
    if (err == ESP_OK && user) {
        err = nvs_set_str(h, NVS_KEY_MQTT_USER, user);
    }
    if (err == ESP_OK && pass) {
        err = nvs_set_str(h, NVS_KEY_MQTT_PASS, pass);
    }
    if (err == ESP_OK && prefix) {
        err = nvs_set_str(h, NVS_KEY_MQTT_PREFIX, prefix);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
