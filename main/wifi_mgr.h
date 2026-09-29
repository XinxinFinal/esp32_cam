#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef enum {
    WIFI_MGR_STATE_IDLE = 0,
    WIFI_MGR_STATE_PROVISIONING,   /* SoftAP 配网中 */
    WIFI_MGR_STATE_CONNECTING,     /* STA 连接中 */
    WIFI_MGR_STATE_CONNECTED,      /* STA 已连接、拿到 IP */
} wifi_mgr_state_t;

/* 初始化 netif + event loop + wifi + 事件处理。app_main 早期调用。 */
esp_err_t wifi_mgr_init(void);

/* 依 NVS 凭据决定启动路径：有凭据→连 STA；无凭据→进配网。 */
esp_err_t wifi_mgr_start(void);

/* 强制进入配网模式（首次 / 清凭据后）。 */
esp_err_t wifi_mgr_enter_provisioning(void);

/* 应用新凭据：写 NVS、关 AP、连 STA。 */
esp_err_t wifi_mgr_apply_credentials(const char *ssid, const char *pass);

/* 停止配网（超时）：有凭据则转连 STA，否则待机等复位。 */
esp_err_t wifi_mgr_stop_provisioning(void);

wifi_mgr_state_t wifi_mgr_get_state(void);

/* 取 AP 接口 IP（配网 DNS 劫持用），写入 buf。 */
esp_err_t wifi_mgr_get_ap_ip(char *buf, size_t len);

/* 取设备主机名 esp32cam-XXXX（网卡 MAC 后 3 字节）。 */
esp_err_t wifi_mgr_get_hostname(char *buf, size_t len);

/* 取 STA 接口 IP（连上后有效）。 */
esp_err_t wifi_mgr_get_sta_ip(char *buf, size_t len);
