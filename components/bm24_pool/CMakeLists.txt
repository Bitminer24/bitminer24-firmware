idf_component_register(
    SRCS "bm24_pool.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_config bm24_miner bm24_network bm24_stratum bm24_work
             esp-tls esp_netif mbedtls
)
