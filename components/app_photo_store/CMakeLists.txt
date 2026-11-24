# app_photo_store_ram.c is the Host simulator backend (listed in game.sim.json only).
# The device picks files (fatfs, "photos" partition) or NVS at run time.
idf_component_register(SRCS "app_photo_store.c" "app_photo_store_device.c" "app_photo_store_nvs.c"
                            "app_photo_store_files.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES app_photo
    PRIV_REQUIRES nvs_flash fatfs esp_partition freertos log)
