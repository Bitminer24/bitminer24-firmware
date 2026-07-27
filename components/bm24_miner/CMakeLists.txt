idf_component_register(
    SRCS "bm24_miner.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_sha bm24_sha_hw bm24_sha_sw bm24_work esp_timer esp_system
)
