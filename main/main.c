#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "nvs_store.h"
#include "boot_counter.h"
#include "wifi_mgr.h"
#include "provisioning.h"
#include "camera.h"
#include "mdns_announce.h"
#include "http_server.h"
#include "mqtt_app.h"

static const char *TAG = "main";

/* 等待 STA 连接后一次性启动业务模块 */
static void app_task(void *arg)
{
    while (1) {
        wifi_mgr_state_t s = wifi_mgr_get_state();
        if (s == WIFI_MGR_STATE_CONNECTED) {
            break;
        }
        if (s == WIFI_MGR_STATE_IDLE) {
            /* 配网超时且无凭据：待机等复位 */
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(TAG, "WiFi 已连接，启动业务模块");

    mdns_announce_init();

    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败，MJPEG 不可用（板型可能不匹配）");
    }
    http_server_start();
    mqtt_client_start();

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_store_init());

    /* 连续 3 次快速重启 → 清凭据进配网 */
    if (boot_counter_check_and_reset_needed()) {
        ESP_LOGW(TAG, "触发清凭据");
        nvs_store_wifi_clear();
    }
    /* 稳定运行超过 settle 秒后计数归零（幂等） */
    boot_counter_start_settle();

    ESP_ERROR_CHECK(wifi_mgr_init());
    ESP_ERROR_CHECK(wifi_mgr_start());

    /* 进入配网模式才起门户 */
    if (wifi_mgr_get_state() == WIFI_MGR_STATE_PROVISIONING) {
        ESP_ERROR_CHECK(provisioning_start());
    }

    xTaskCreate(app_task, "app", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
