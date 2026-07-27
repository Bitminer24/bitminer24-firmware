idf_component_register(
    SRCS "bm24_stratum_wire.c" "bm24_stratum_json.c"
    INCLUDE_DIRS "include"
    REQUIRES bm24_work json
)
