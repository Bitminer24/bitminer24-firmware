idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS ""
    REQUIRES bm24_format bm24_sha bm24_sha_sw bm24_sha_hw esp_system esp_driver_tsens
)
