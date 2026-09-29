#include "esp_camera.h"
#include "esp_log.h"
#include "camera.h"

static const char *TAG = "camera";

/* AI-Thinker ESP32-CAM 引脚定义（OV2640） */
#define PIN_PWDN   32
#define PIN_RESET  -1
#define PIN_XCLK   0
#define PIN_SIOD   26   /* SCCB SDA */
#define PIN_SIOC   27   /* SCCB SCL */
#define PIN_D7     35
#define PIN_D6     34
#define PIN_D5     39
#define PIN_D4     36
#define PIN_D3     21
#define PIN_D2     19
#define PIN_D1     18
#define PIN_D0     5
#define PIN_VSYNC  25
#define PIN_HREF   23
#define PIN_PCLK   22

/* 图像参数：VGA 640x480，JPEG 质量 10，双帧缓冲 */
#define CAM_XCLK_FREQ    20000000
#define CAM_FRAME_SIZE   FRAMESIZE_VGA
#define CAM_JPEG_QUALITY 10
#define CAM_FB_COUNT     2

esp_err_t camera_init(void)
{
    camera_config_t cfg = {
        .pin_pwdn     = PIN_PWDN,
        .pin_reset    = PIN_RESET,
        .pin_xclk     = PIN_XCLK,
        .pin_sccb_sda = PIN_SIOD,
        .pin_sccb_scl = PIN_SIOC,

        .pin_d7 = PIN_D7, .pin_d6 = PIN_D6, .pin_d5 = PIN_D5, .pin_d4 = PIN_D4,
        .pin_d3 = PIN_D3, .pin_d2 = PIN_D2, .pin_d1 = PIN_D1, .pin_d0 = PIN_D0,
        .pin_vsync = PIN_VSYNC, .pin_href = PIN_HREF, .pin_pclk = PIN_PCLK,

        .xclk_freq_hz = CAM_XCLK_FREQ,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = CAM_FRAME_SIZE,
        .jpeg_quality = CAM_JPEG_QUALITY,
        .fb_count     = CAM_FB_COUNT,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        /* 若此处失败且丝印是 ESP32S，多半是板型推断错误（非原版 ESP32-CAM） */
        ESP_LOGE(TAG, "esp_camera_init 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "摄像头初始化成功 (VGA / JPEG)");
    return ESP_OK;
}
