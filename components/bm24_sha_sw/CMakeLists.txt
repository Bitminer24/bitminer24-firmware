idf_component_register(
    SRCS "bm24_sha_sw.c"
    INCLUDE_DIRS "include"
)

# Hot-Path: -O3 und Unrolling wie in 1.x vermessen; das Framework baut
# sonst mit -Os. Die Pragmas im Quelltext decken GCC ab, das Flag hier
# stellt sicher, dass keine Build-Konfiguration sie aushebelt.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -funroll-loops)
