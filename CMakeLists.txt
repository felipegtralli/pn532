idf_component_register(SRCS "src/pn532.c" "src/pn532_uart.c" "src/pn532_internal.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio esp_driver_uart
                    PRIV_REQUIRES freertos)
