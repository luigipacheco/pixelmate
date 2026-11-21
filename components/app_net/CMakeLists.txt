idf_component_register(SRCS "app_net.c" "app_net_web.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_http_server log mdns
        esp_mosaico_app_recovery)
