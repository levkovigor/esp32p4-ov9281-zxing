idf_component_register(SRCS "main.c" "app_video.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES zxing esp_driver_ppa)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov9281_detect")
