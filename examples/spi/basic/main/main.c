#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532_spi.h"

#define MISO  19
#define MOSI  23
#define SCK   18
#define CS    5
#define SPEED 2000000 // 2 MHz

void app_main(void) {
    // 1. Create PN532 SPI Object Instance (Constructor)
    pn532_spi_t *nfc = pn532_spi_create(CS, MISO, MOSI, SCK, SPEED);
    if (!nfc) {
        printf("Failed to create PN532 SPI instance!\n");
        return;
    }

    // 2. Initialize PN532 SPI Hardware
    esp_err_t err = nfc->begin(nfc);
    if (err != ESP_OK) {
        printf("Failed to initialize PN532 SPI device! (err: %d)\n", err);
    }

    printf("\n--- Diagnostics Complete. Entering Card-Detection Loop ---\n");

    // 3. Infinite Card Scan Loop
    while (1) {
        if (nfc->reader_status(nfc) == ESP_OK) {
            unsigned long card = nfc->get_pn532_data(nfc);
            if (card == (unsigned long)-2) {
                printf("\n Invalid card data \n");
                continue;
            }
            if (card == (unsigned long)-1) {
                // Timeout scanning (normal when no card present)
                continue;
            }
            printf("\n Card detected!\n");
            printf("  Card ID (Decimal): %lu\n\n", card);
        } else {
            printf("  Failed to communicate with reader. Retrying...\n");
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay 500ms before scanning again
    }

    // 4. Destroy Object Instance (Destructor)
    pn532_spi_destroy(nfc);
}
