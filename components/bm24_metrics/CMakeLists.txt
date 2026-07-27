idf_component_register(
    SRCS "bm24_metrics.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_miner bm24_network esp_http_client esp-tls json
)
