idf_component_register(
    SRCS "bm24_display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_gpio freertos
)
