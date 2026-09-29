
# ESP32-CAM 家庭摄像头

基于 ESP-IDF v5.x 的家庭摄像头固件。**实时画面留局域网（MJPEG），事件上云（MQTT）**。

## 硬件

- AI-Thinker ESP32-CAM（ESP32-D0WD + 4MB flash + 4MB PSRAM + OV2640）

> ⚠️ **板型是推断的**：模组丝印 `ESP32S` 对应 AI-Thinker 原版 ESP32-CAM，不是 Espressif 的
> ESP32-S2/S3。若推断错误，首次 `esp_camera_init()` 会直接失败，届时即可识别。

## 架构

| 项 | 方案 |
|---|---|
| 配网 | SoftAP + 网页门户 + Captive Portal 自动弹窗 |
| 实时画面 | 局域网 MJPEG（`/stream`），HA 用 `mjpeg` 集成拉流 |
| 事件上云 | Mosquitto MQTT，只传状态/控制/诊断，不推图片 |
| mDNS | `esp32cam-xxxx.local` |

### 关键取舍

- **画面留局域网、事件上云**：ESP32 跑起 WiFi 后可用堆仅 290–320KB，连续视频上云（RTMP/WebRTC）撑不住，故 MJPEG 走局域网，MQTT 只传轻量数据。
- **局域网 MQTT 默认不上 TLS**（省 35–45KB 堆），留 `CONFIG_APP_MQTT_TLS` 开关。
- **WiFi 断连只做指数退避重试，不自动退回配网**：路由器重启不应把摄像头打回 AP 模式。
- **清凭据靠连续 3 次快速重启**（板上除 RST 无可用按键，GPIO 被摄像头和 TF 卡占满）。

## 配网流程

1. 上电。无凭据时进入 SoftAP：`ESP32CAM-Setup-XXXXXX`，默认密码 `esp32cam`（Kconfig 可改）。
2. 手机连上该热点，自动弹出配网页（Captive Portal）。
3. 填 WiFi SSID/密码 + MQTT 地址/端口/账号/前缀，提交。
4. 设备保存配置 → 关闭热点 → 连 WiFi → 起摄像头 / MJPEG / MQTT。

安全加固：热点带密码 + 5 分钟超时 + 限 1 客户端 + 配网成功即关 AP。配网页在热点阶段是
HTTP 明文，WiFi 密码会明文过那段链路（已确认接受该取舍）。

## MQTT 主题与 HA

主题前缀默认 `esp32cam`（配网可改）：

| 主题 | 方向 | 说明 |
|---|---|---|
| `<prefix>/status` | 发布 | `online`/`offline`（LWT，retained，作 availability） |
| `<prefix>/tele/heap` | 发布 | 空闲堆（B，60s 周期） |
| `<prefix>/tele/rssi` | 发布 | WiFi 信号（dBm） |
| `<prefix>/tele/uptime` | 发布 | 运行时长（s） |
| `<prefix>/cmd/reboot` | 订阅 | 收到即重启 |

上电后自动向 `homeassistant/...` 发布 **HA MQTT Discovery**，HA 会自动创建：
`Free Heap`、`WiFi RSSI`、`Uptime` 三个传感器 + `Reboot` 按钮。

> MJPEG 摄像头实体需要手动在 HA 添加 `mjpeg` 集成，URL 填
> `http://esp32cam-xxxx.local/stream`（或直接用 IP）。

## 目录结构

```
main/
├── main.c            # 编排：启动计数 → WiFi → 配网/业务
├── app_config.h      # NVS 键名、常量、app_config_t
├── nvs_store.c/.h    # NVS 读写（WiFi/MQTT 凭据）
├── boot_counter.c/.h # 3 次快速重启清凭据
├── wifi_mgr.c/.h     # WiFi 状态机（STA/AP、退避重连）
├── provisioning.c/.h # 配网门户（HTTP + Captive Portal DNS）
├── camera.c/.h       # OV2640 初始化
├── http_server.c/.h  # 本地 Web（状态页 + MJPEG）
├── mdns_announce.c/.h# mDNS 广播
└── mqtt_client.c/.h  # MQTT + HA Discovery
```

## 构建与烧录

```bash
# 装好 ESP-IDF v5.x 并 source export.sh 后：
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

首次构建会从组件仓库拉取 `espressif/esp32-camera`。

## 常用配置（`idf.py menuconfig` → ESP32-CAM Home Camera）

| 配置 | 默认 | 说明 |
|---|---|---|
| `APP_PROV_AP_PASSWORD` | `esp32cam` | 配网热点密码 |
| `APP_PROV_AP_TIMEOUT_SEC` | `300` | 热点超时（秒） |
| `APP_BOOT_LOOP_THRESHOLD` | `3` | 快速重启清凭据阈值 |
| `APP_BOOT_LOOP_SETTLE_SEC` | `30` | 稳定运行后计数归零 |
| `APP_RECONNECT_BACKOFF_MAX_SEC` | `60` | WiFi 重连退避上限 |
| `APP_MQTT_TLS` | `n` | MQTT 是否启用 TLS |

## 状态

- [x] 工程骨架 / 分区表 / sdkconfig
- [x] NVS 存储 + 启动计数（3 次快速重启清凭据）
- [x] WiFi 状态机（指数退避重连）
- [x] 配网门户（SoftAP + HTTP + Captive Portal + DNS）
- [x] 摄像头 + 本地 HTTP/MJPEG + mDNS
- [x] MQTT + Home Assistant Discovery
- [ ] 移动侦测（v2 计划）
