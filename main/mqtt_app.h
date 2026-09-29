#pragma once

#include "esp_err.h"

/* 连接 MQTT（若已配置 host）。内部自管重连、HA Discovery、遥测发布。 */
esp_err_t mqtt_client_start(void);
