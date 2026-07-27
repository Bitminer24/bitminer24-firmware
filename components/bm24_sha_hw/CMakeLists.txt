idf_component_register(
    SRCS "bm24_sha_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_sha mbedtls hal soc esp_hw_support
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
