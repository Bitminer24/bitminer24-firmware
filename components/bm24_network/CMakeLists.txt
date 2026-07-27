idf_component_register(
    SRCS "bm24_network.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_config app_update esp_app_format esp_event esp_http_server
             esp_netif esp_wifi freertos
)
