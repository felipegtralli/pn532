#ifndef PN532_INTERNAL_H
#define PN532_INTERNAL_H

#include "pn532.h"

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#if CONFIG_LOG_DEFAULT_LEVEL >= 4 // 4 = LOG_LEVEL_DEBUG
#define PN532_DEBUG
#endif

#define PN532_BUFFER_SIZE 256

typedef esp_err_t (*pn532_start_fn)(struct PN532 *pn532);
typedef esp_err_t (*pn532_write_command_fn)(struct PN532 *pn532, uint8_t *command, uint8_t command_len);
typedef esp_err_t (*pn532_read_response_fn)(struct PN532 *pn532, TickType_t timeout);
typedef esp_err_t (*pn532_free_fn)(struct PN532 *pn532);

typedef struct
{
    uart_port_t uart_port;
} PN532UartContext;

typedef struct
{
    // todo
} PN532I2CContext;

typedef struct
{
    // todo
} PN532SpiContext;

typedef struct PN532
{
    uint8_t buffer[PN532_BUFFER_SIZE];

    PN532Protocol protocol;
    union
    {
        PN532UartContext uart;
        PN532I2CContext i2c;
        PN532SpiContext spi;
    };

    SemaphoreHandle_t mutex;

    pn532_start_fn start;
    pn532_write_command_fn write_command;
    pn532_read_response_fn read_response;
    pn532_free_fn free;
} PN532;

esp_err_t pn532_uart_init(PN532 *pn532, const PN532UartConfig *config);

#endif // PN532_INTERNAL_H
