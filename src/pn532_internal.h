#ifndef PN532_INTERNAL_H
#define PN532_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pn532.h"

#define PN532_BUFFER_SIZE 256

#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00

#define PN532_FRAME_PREAMBLE_OFFSET 0U
#define PN532_FRAME_STARTCODE1_OFFSET 1U
#define PN532_FRAME_STARTCODE2_OFFSET 2U
#define PN532_FRAME_LEN_OFFSET 3U
#define PN532_FRAME_LCS_OFFSET 4U

#define PN532_FRAME_HEADER_SIZE 5U
#define PN532_FRAME_TRAILER_SIZE 2U
#define PN532_FRAME_OVERHEAD 8U

#define PN532_NORMAL_COMMAND_MAX_LEN 254U

typedef esp_err_t (*pn532_start_fn)(struct PN532 *pn532);
typedef esp_err_t (*pn532_write_command_fn)(struct PN532 *pn532, const uint8_t *command, uint8_t command_len);
typedef esp_err_t (*pn532_read_response_fn)(struct PN532 *pn532, uint8_t *buffer, size_t buffer_size, size_t *bytes_read, TickType_t timeout);
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

/* UART functions */
esp_err_t pn532_uart_init(PN532 *pn532, const PN532UartConfig *config);

/* PN532 common functions */
size_t pn532_command_framer(const uint8_t *command, size_t command_len, uint8_t *frame);
esp_err_t pn532_response_parser(const uint8_t *frame, size_t frame_len, uint8_t expected_command, const uint8_t **payload, size_t *payload_len);

#endif // PN532_INTERNAL_H
