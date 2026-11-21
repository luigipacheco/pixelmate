# app_photo_store_ram.c is the Host simulator backend (listed in game.sim.json only).
idf_component_register(SRCS "app_photo_store.c" "app_photo_store_nvs.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES app_photo
    PRIV_REQUIRES nvs_flash freertos log)
