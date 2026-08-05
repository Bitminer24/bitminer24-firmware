idf_component_register(
    SRCS "bm24_network.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_config bm24_identity esp_event esp_http_server mbedtls mdns
             esp_netif esp_wifi freertos
)
