#include "pn532.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_LOG_DEFAULT_LEVEL >= 4 // 4 = LOG_LEVEL_DEBUG
    #define PN532_DEBUG
#endif

#define PN532_DELAY_DEFAULT (500 / portTICK_PERIOD_MS)
#define PN532_DELAY(ms) (ms / portTICK_PERIOD_MS)

typedef struct pn532_t {
    pn532_protocol_t protocol;
    union {
        uart_port_t uart_port;
        // todo i2c/spi specifics
    };
    SemaphoreHandle_t mutex;
    esp_err_t (*write_command)(struct pn532_t* pn532, uint8_t* command, uint8_t command_len);
    esp_err_t (*read_response)(struct pn532_t* pn532, uint8_t* response, size_t response_len);
    esp_err_t (*free)(struct pn532_t* pn532);
} pn532_t;