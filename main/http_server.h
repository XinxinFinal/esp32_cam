#pragma once

#include "esp_err.h"

/* 启动本地 Web 服务（STA 接口，80 端口）：状态页 / + MJPEG 流 /stream。 */
esp_err_t http_server_start(void);
