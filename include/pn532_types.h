#include "pn532.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_LOG_DEFAULT_LEVEL >= 4 // 4 = LOG_LEVEL_DEBUG
    #define PN532_DEBUG
#endif

#define PN532_BUFFER_SIZE 256

typedef struct {
    uart_port_t uart_port;
} uart_specifics_t;

typedef struct {
    // todo
} i2c_specifics_t;

typedef struct {
    // todo
} spi_specifics_t;

typedef struct pn532_t {
    uint8_t buffer[PN532_BUFFER_SIZE];
    pn532_protocol_t protocol;
    union {
        uart_specifics_t uart;
        i2c_specifics_t i2c;
        spi_specifics_t spi;
    };
    SemaphoreHandle_t mutex;
    esp_err_t (*write_command)(struct pn532_t* pn532, uint8_t* command, uint8_t command_len);
    esp_err_t (*read_response)(struct pn532_t* pn532, TickType_t timeout);
    esp_err_t (*free)(struct pn532_t* pn532);
} pn532_t;