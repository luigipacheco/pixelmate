idf_component_register(SRCS "app_sfx.c"
    INCLUDE_DIRS "include"
    REQUIRES app_game
    PRIV_REQUIRES raylib-lite-engine examples_audio log)
