idf_component_register(SRCS "src/pn532.c" "src/pn532_uart.c" "src/pn532_spi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver freertos esp_driver_gptimer)
