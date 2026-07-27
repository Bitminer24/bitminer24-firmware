idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS ""
    REQUIRES bm24_config bm24_display bm24_miner bm24_network bm24_pool
             app_update esp_app_format esp_driver_gpio esp_driver_tsens
             esp_system nvs_flash
)
