idf_component_register(SRCS "LD2410C.c" "main.c" "led_ws2812b.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_rmt esp_driver_uart esp_driver_gpio esp_timer esp_adc)
