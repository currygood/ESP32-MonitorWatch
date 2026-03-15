set(src_dirs
    LED/LED.c
	I2C/i2c_driver.c)

set(include_dirs
    LED
	I2C)

set(requires
    driver)

idf_component_register(SRCS "${src_dirs}"
					   INCLUDE_DIRS "${include_dirs}"
					   REQUIRES "${requires}")
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)