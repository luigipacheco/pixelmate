idf_component_register(SRCS "app_net.c" "app_net_web.c" "app_net_portal.c" "app_net_dns.c" "app_net_photos.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_http_server esp_hw_support log lwip mdns nvs_flash
        esp_mosaico_app_recovery app_photo app_photo_store heap)
