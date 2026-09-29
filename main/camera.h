#pragma once

#include "esp_err.h"

/* 初始化 OV2640 摄像头（AI-Thinker ESP32-CAM 引脚 + 4MB PSRAM）。 */
esp_err_t camera_init(void);
