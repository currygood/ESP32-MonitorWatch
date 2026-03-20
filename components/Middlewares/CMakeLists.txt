set(src_dirs
    MAX30102/max30102.c
	OLED/OLED.c
	OLED/OLED_Font.c
	MPU6050/MPU6050.c)

set(include_dirs
	OLED
    MAX30102
    MPU6050)

set(requires
    driver
    BSP)

idf_component_register(SRCS "${src_dirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES ${requires}
					   REQUIRES esp_timer driver)

component_compile_options(-ffast-math -O3 -Wno-format)
