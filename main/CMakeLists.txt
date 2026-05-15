idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES BSP Middlewares)
					
spiffs_create_partition_image(storage ../html FLASH_IN_PROJECT)