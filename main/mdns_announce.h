#pragma once

#include "esp_err.h"

/* 初始化 mDNS：主机名 esp32cam-XXXX.local，广告 _http._tcp。 */
esp_err_t mdns_announce_init(void);
