#pragma once

#include "esp_err.h"

/* 启动配网门户任务。仅在 WiFi 已进入配网模式时调用（见 main.c）。
 * 任务职责：起 DNS 劫持 + HTTP 表单 → 等待用户提交或超时 → 关闭服务器，
 * 并由表单提交触发 WiFi 切换到 STA（超时则转待机/重连）。 */
esp_err_t provisioning_start(void);
