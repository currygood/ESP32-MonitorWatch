idf_component_register(SRCS "signal_fusion.c" "seizure_model.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES Middlewares BSP spiffs espressif__esp-tflite-micro)