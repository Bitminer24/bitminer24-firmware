idf_component_register(
    SRCS "bm24_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_display bm24_format bm24_metrics bm24_miner bm24_network
             bm24_pool esp_driver_gpio esp_timer freertos
)
