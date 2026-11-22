# Vibe Mode's shared Wi-Fi values exist only on the ESP-Mosaico.
set(app_net_vibe)
if(IDF_TARGET STREQUAL "esp32s31")
    set(app_net_vibe esp_mosaico_app_recovery)
endif()
idf_component_register(SRCS "app_net.c" "app_net_web.c" "app_net_portal.c" "app_net_dns.c" "app_net_photos.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_http_server esp_hw_support log lwip mdns nvs_flash
        ${app_net_vibe} app_photo app_photo_store heap)
if(app_net_vibe)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_NET_VIBE_SHARED=1)
endif()
