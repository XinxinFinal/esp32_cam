#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <mqtt_client.h>
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "app_config.h"
#include "nvs_store.h"
#include "wifi_mgr.h"
#include "mqtt_app.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;

/* 连接参数（须为静态/堆内存，esp_mqtt 内部持有指针不深拷贝） */
static char s_prefix[32] = APP_MQTT_DEFAULT_TOPIC_PREFIX;
static char s_hostname[32] = "";
static char s_uri[96] = "";
static char s_user[33] = "";
static char s_pass[33] = "";
static char s_lwt_topic[64] = "";
static char s_cmd_topic[64] = "";

static void publish_discovery(void);
static void publish_telemetry(void);

/* ---------------- 事件处理 ---------------- */

static void handle_data(esp_mqtt_event_handle_t ev)
{
    char topic[96];
    int tlen = ev->topic_len < (int)sizeof(topic) - 1 ? ev->topic_len : (int)sizeof(topic) - 1;
    memcpy(topic, ev->topic, tlen);
    topic[tlen] = '\0';

    char reboot_topic[64];
    snprintf(reboot_topic, sizeof(reboot_topic), "%s/cmd/reboot", s_prefix);
    if (strcmp(topic, reboot_topic) == 0) {
        ESP_LOGW(TAG, "收到重启命令，即将重启");
        esp_mqtt_client_publish(s_client, reboot_topic, "OFF", 3, 1, 0);
        esp_restart();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT 已连接");
        esp_mqtt_client_subscribe(s_client, s_cmd_topic, 0);
        esp_mqtt_client_publish(s_client, s_lwt_topic, "online", 6, 1, 1);
        publish_discovery();
        break;
    case MQTT_EVENT_DATA:
        handle_data((esp_mqtt_event_handle_t)event_data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 断开，等待自动重连");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT 错误");
        break;
    default:
        break;
    }
}

/* ---------------- Discovery / 遥测 ---------------- */

static void publish_discovery(void)
{
    char topic[128];
    char payload[512];

    /* 传感器：空闲堆 */
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/free_heap/config", s_hostname);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"Free Heap\",\"state_topic\":\"%s/tele/heap\","
             "\"unit_of_measurement\":\"B\",\"device_class\":\"data_size\","
             "\"unique_id\":\"%s_free_heap\",\"availability_topic\":\"%s/status\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"ESP32-CAM %s\","
             "\"model\":\"ESP32-CAM\",\"manufacturer\":\"AI-Thinker\"}}",
             s_prefix, s_hostname, s_prefix, s_hostname, s_hostname);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);

    /* 传感器：WiFi 信号 */
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/rssi/config", s_hostname);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"WiFi RSSI\",\"state_topic\":\"%s/tele/rssi\","
             "\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\","
             "\"unique_id\":\"%s_rssi\",\"availability_topic\":\"%s/status\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"ESP32-CAM %s\","
             "\"model\":\"ESP32-CAM\",\"manufacturer\":\"AI-Thinker\"}}",
             s_prefix, s_hostname, s_prefix, s_hostname, s_hostname);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);

    /* 传感器：运行时长 */
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/uptime/config", s_hostname);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"Uptime\",\"state_topic\":\"%s/tele/uptime\","
             "\"unit_of_measurement\":\"s\","
             "\"unique_id\":\"%s_uptime\",\"availability_topic\":\"%s/status\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"ESP32-CAM %s\","
             "\"model\":\"ESP32-CAM\",\"manufacturer\":\"AI-Thinker\"}}",
             s_prefix, s_hostname, s_prefix, s_hostname, s_hostname);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);

    /* 按钮：重启 */
    snprintf(topic, sizeof(topic), "homeassistant/button/%s/reboot/config", s_hostname);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"Reboot\",\"command_topic\":\"%s/cmd/reboot\","
             "\"unique_id\":\"%s_reboot\",\"availability_topic\":\"%s/status\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"ESP32-CAM %s\","
             "\"model\":\"ESP32-CAM\",\"manufacturer\":\"AI-Thinker\"}}",
             s_prefix, s_hostname, s_prefix, s_hostname, s_hostname);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);

    ESP_LOGI(TAG, "HA Discovery 已发布");
}

static void publish_telemetry(void)
{
    char topic[64], val[32];

    snprintf(topic, sizeof(topic), "%s/tele/heap", s_prefix);
    snprintf(val, sizeof(val), "%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    esp_mqtt_client_publish(s_client, topic, val, 0, 0, 0);

    wifi_ap_record_t ap = {0};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    snprintf(topic, sizeof(topic), "%s/tele/rssi", s_prefix);
    snprintf(val, sizeof(val), "%d", rssi);
    esp_mqtt_client_publish(s_client, topic, val, 0, 0, 0);

    snprintf(topic, sizeof(topic), "%s/tele/uptime", s_prefix);
    snprintf(val, sizeof(val), "%lld", (long long)(esp_timer_get_time() / 1000000));
    esp_mqtt_client_publish(s_client, topic, val, 0, 0, 0);
}

static void telemetry_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        if (s_client != NULL) {
            publish_telemetry();
        }
    }
}

/* ---------------- 启动 ---------------- */

esp_err_t mqtt_client_start(void)
{
    char host[64] = "";
    uint16_t port = APP_MQTT_DEFAULT_PORT;
    char user[33] = "", pass[33] = "";

    if (nvs_store_mqtt_get(host, sizeof(host), &port, user, sizeof(user),
                           pass, sizeof(pass), s_prefix, sizeof(s_prefix)) != ESP_OK) {
        ESP_LOGW(TAG, "未配置 MQTT，跳过");
        return ESP_OK;
    }
    if (host[0] == '\0') {
        ESP_LOGW(TAG, "MQTT host 为空，跳过");
        return ESP_OK;
    }

    wifi_mgr_get_hostname(s_hostname, sizeof(s_hostname));

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             CONFIG_APP_MQTT_TLS ? "mqtts" : "mqtt", host, (unsigned)port);
    strlcpy(s_user, user, sizeof(s_user));
    strlcpy(s_pass, pass, sizeof(s_pass));
    snprintf(s_lwt_topic, sizeof(s_lwt_topic), "%s/status", s_prefix);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "%s/cmd/#", s_prefix);

    /* 注意：字段名随 IDF 5.x 小版本略有差异，以本机 esp_mqtt.h 为准 */
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.username = s_user[0] ? s_user : NULL,
        .credentials.authentication.password = s_pass[0] ? s_pass : NULL,
        .session.last_will.topic = s_lwt_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .session.keepalive = 60,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init 失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_CONNECTED, mqtt_event_handler, NULL);
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_DATA, mqtt_event_handler, NULL);
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_DISCONNECTED, mqtt_event_handler, NULL);
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ERROR, mqtt_event_handler, NULL);

    esp_mqtt_client_start(s_client);
    xTaskCreate(telemetry_task, "mqtt_tele", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "MQTT 启动: %s (前缀 %s)", s_uri, s_prefix);
    return ESP_OK;
}
