#include "pn532.h"

#define UART_TX_PIN    CONFIG_UART_TX_GPIO_PIN
#define UART_RX_PIN    CONFIG_UART_RX_GPIO_PIN
#define UART_PORT      CONFIG_UART_PORT
#define UART_BAUD_RATE CONFIG_UART_BAUD_RATE

void app_main() {
    pn532_config_t pn532_config = {
        .protocol = PN532_UART_PROTOCOL,
        .uart = {
            .tx = UART_TX_PIN,
            .rx = UART_RX_PIN,
            .uart_port = UART_PORT,
            .baud_rate = UART_BAUD_RATE,
        },
    };
    pn532_handle_t pn532 = NULL;
    ESP_ERROR_CHECK(pn532_init(&pn532, &pn532_config)); // use pn532_free() after done
}