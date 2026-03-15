set(src_dirs
    MAX30102/max30102.c)

set(include_dirs
    MAX30102)

set(requires
    driver
    BSP)

idf_component_register(SRCS "${src_dirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-format)
