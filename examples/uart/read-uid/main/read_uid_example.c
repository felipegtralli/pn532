#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "pn532.h"

#define MAX_READING_FAIL 100

#define UART_TX_PIN    CONFIG_UART_TX_GPIO_PIN
#define UART_RX_PIN    CONFIG_UART_RX_GPIO_PIN
#define UART_PORT      CONFIG_UART_PORT
#define UART_BAUD_RATE CONFIG_UART_BAUD_RATE

volatile uint16_t fail_count = 0;

static const char* TAG = "example";

void example_task(void* pvParameters) {
    pn532_handle_t pn532 = (pn532_handle_t) pvParameters;

    ESP_ERROR_CHECK(pn532_start(pn532));

    uint8_t version[4] = {0}; // version len must be 4
    ESP_ERROR_CHECK(pn532_get_firmware_version(pn532, version));

    ESP_LOGI(TAG, "CHIP PN5%02X\n", version[0]);
    ESP_LOGI(TAG, "Firmware version %02X.%02X\n", version[1], version[2]);

    ESP_ERROR_CHECK(pn532_SAM_configuration(pn532));

    uint8_t uid[10] = {0};
    size_t uid_len = sizeof(uid);
    while(true) {
        esp_err_t err = pn532_read_passive_target_id(pn532, PN532_MIFARE_ISO14443A, uid, &uid_len);
        if(err == ESP_OK) {
            ESP_LOGI(TAG, "UID read: ");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, uid, uid_len, ESP_LOG_INFO);

            fail_count = 0;
        } else {
            if(++fail_count > MAX_READING_FAIL) {
                break;
            }            
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "ended example task");

    pn532_free(pn532);
    vTaskDelete(NULL);
}

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
    ESP_ERROR_CHECK(pn532_init(&pn532, &pn532_config));

    xTaskCreate(example_task, "example", 4096, (void*) pn532, 5, NULL);
}