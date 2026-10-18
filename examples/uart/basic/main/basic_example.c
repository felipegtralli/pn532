#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "pn532.h"

#define UART_TX_PIN CONFIG_UART_TX_GPIO_PIN
#define UART_RX_PIN CONFIG_UART_RX_GPIO_PIN
#define UART_PORT CONFIG_UART_PORT

static const char *TAG = "basic-example";

void app_main(void)
{
    /* UART TX/RX pins are from the ESP32's perspective; cross them with the module's RX/TX. */
    const PN532Config pn532_config = {
        .protocol = PN532_UART_PROTOCOL,
        .uart = {
            .tx = UART_TX_PIN,
            .rx = UART_RX_PIN,
            .uart_port = UART_PORT,
        },
    };
    PN532Handle pn532 = NULL;

    ESP_ERROR_CHECK(pn532_init(&pn532, &pn532_config));

    /* Wake the PN532, then configure SAM Normal mode. */
    ESP_ERROR_CHECK(pn532_start(pn532));
    ESP_ERROR_CHECK(pn532_SAM_configuration(pn532));

    /* Firmware response: chip ID, version, revision, supported features. */
    uint8_t version_buffer[4] = {0};
    ESP_ERROR_CHECK(pn532_get_firmware_version(pn532, version_buffer, sizeof(version_buffer)));

    ESP_LOGI(TAG, "Firmware IC=0x%02X, version=%u.%u, support=0x%02X", (unsigned int)version_buffer[0], (unsigned int)version_buffer[1], (unsigned int)version_buffer[2], (unsigned int)version_buffer[3]);

    ESP_LOGI(TAG, "PN532 basic example finished");

    for (unsigned int seconds = 5U; seconds > 0U; seconds--)
    {
        ESP_LOGI(TAG, "Freeing resources in %u s", seconds);
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    ESP_ERROR_CHECK(pn532_free(pn532));
    ESP_LOGI(TAG, "Resources freed; returning");
}
