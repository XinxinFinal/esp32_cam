#pragma once

#include <stdint.h>
#include <stdbool.h>

/* NVS 命名空间 */
#define NVS_NS_WIFI   "wifi"
#define NVS_NS_MQTT   "mqtt"
#define NVS_NS_BOOT   "boot"

/* NVS 键 */
#define NVS_KEY_WIFI_SSID   "ssid"
#define NVS_KEY_WIFI_PASS   "pass"
#define NVS_KEY_MQTT_HOST   "host"
#define NVS_KEY_MQTT_PORT   "port"
#define NVS_KEY_MQTT_USER   "user"
#define NVS_KEY_MQTT_PASS   "pass"
#define NVS_KEY_MQTT_PREFIX "prefix"
#define NVS_KEY_BOOT_COUNT  "count"

/* 设备标识 */
#define APP_HOSTNAME_PREFIX   "esp32cam"

/* 配网 AP */
#define APP_AP_SSID_PREFIX    "ESP32CAM-Setup-"
#define APP_AP_MAX_STA_CONN   1

/* MQTT 默认值（配网页可覆盖） */
#define APP_MQTT_DEFAULT_PORT         1883
#define APP_MQTT_DEFAULT_TOPIC_PREFIX "esp32cam"

/* CONFIG_APP_MQTT_TLS 为 bool、默认 n；未开启时 Kconfig 不生成该宏，
   这里兜底为 0，供代码用三元表达式直接判断。 */
#ifndef CONFIG_APP_MQTT_TLS
#define CONFIG_APP_MQTT_TLS 0
#endif

/* 一次配网提交的完整配置 */
typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];
    char     mqtt_host[64];
    uint16_t mqtt_port;
    char     mqtt_user[33];
    char     mqtt_pass[33];
    char     mqtt_topic_prefix[32];
} app_config_t;
