idf_component_register(
    SRCS "main.c" "nvs_store.c" "boot_counter.c" "wifi_mgr.c" "provisioning.c"
         "camera.c" "mdns_announce.c" "http_server.c" "mqtt_client.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_timer esp_http_server lwip
              mdns mqtt esp32-camera
)
