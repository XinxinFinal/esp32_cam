#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 初始化 NVS 闪存（必要时擦除重建） */
esp_err_t nvs_store_init(void);

/* ---- WiFi 凭据 ---- */
bool      nvs_store_has_wifi(void);
esp_err_t nvs_store_wifi_get(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_store_wifi_set(const char *ssid, const char *pass);
esp_err_t nvs_store_wifi_clear(void);

/* ---- MQTT 配置 ---- */
esp_err_t nvs_store_mqtt_get(char *host, size_t host_len, uint16_t *port,
                             char *user, size_t user_len, char *pass, size_t pass_len,
                             char *prefix, size_t prefix_len);
esp_err_t nvs_store_mqtt_set(const char *host, uint16_t port,
                             const char *user, const char *pass, const char *prefix);
