#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "boot_counter.h"

static const char *TAG = "boot";
static esp_timer_handle_t s_settle_timer = NULL;

static uint32_t read_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BOOT, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    uint32_t c = 0;
    if (nvs_get_u32(h, NVS_KEY_BOOT_COUNT, &c) != ESP_OK) {
        c = 0;
    }
    nvs_close(h);
    return c;
}

static void write_count(uint32_t c)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BOOT, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u32(h, NVS_KEY_BOOT_COUNT, c);
    nvs_commit(h);
    nvs_close(h);
}

bool boot_counter_check_and_reset_needed(void)
{
    uint32_t c = read_count() + 1;
    if (c >= (uint32_t)CONFIG_APP_BOOT_LOOP_THRESHOLD) {
        write_count(0); /* 清零，避免下次开机又触发 */
        ESP_LOGW(TAG, "检测到连续快速重启（count=%" PRIu32 "），触发清凭据", c);
        return true;
    }
    write_count(c);
    ESP_LOGI(TAG, "启动计数 = %" PRIu32, c);
    return false;
}

static void settle_cb(void *arg)
{
    write_count(0);
    ESP_LOGI(TAG, "已稳定运行，启动计数归零");
}

void boot_counter_start_settle(void)
{
    if (s_settle_timer != NULL) {
        return;
    }
    const esp_timer_create_args_t args = {
        .callback = settle_cb,
        .name = "boot_settle",
    };
    if (esp_timer_create(&args, &s_settle_timer) == ESP_OK) {
        esp_timer_start_once(s_settle_timer,
                             (uint64_t)CONFIG_APP_BOOT_LOOP_SETTLE_SEC * 1000000ULL);
    }
}
