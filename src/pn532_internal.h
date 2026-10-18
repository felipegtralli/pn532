/**
 * @file pn532_internal.h
 * @brief Private driver state, transport callbacks, and shared frame helpers.
 *
 * The core holds mutex across start and command exchanges; transport callbacks
 * must not take it again. Initialization and cleanup run outside that lock and
 * rely on caller-coordinated handle lifetime.
 */

#ifndef PN532_INTERNAL_H
#define PN532_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pn532.h"

#define PN532_BUFFER_SIZE 256U /**< Per-handle scratch and receive capacity in bytes. */

/* Normal-frame fields and offsets: UM0701-02 Rev. 02, Section 6.2.1.1, p. 28. */
#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00

#define PN532_FRAME_PREAMBLE_OFFSET 0U
#define PN532_FRAME_STARTCODE1_OFFSET 1U
#define PN532_FRAME_STARTCODE2_OFFSET 2U
#define PN532_FRAME_LEN_OFFSET 3U
#define PN532_FRAME_LCS_OFFSET 4U

#define PN532_FRAME_HEADER_SIZE 5U  /**< Preamble, two start-code bytes, LEN, and LCS. */
#define PN532_FRAME_TRAILER_SIZE 2U /**< Data checksum and postamble. */
#define PN532_FRAME_OVERHEAD 8U     /**< Bytes added around command bytes, including TFI. */

#define PN532_NORMAL_COMMAND_MAX_LEN 254U /**< Command ID and parameters; LEN also includes one TFI byte. */

/** @brief Perform the selected transport's wake-up operation. */
typedef esp_err_t (*pn532_start_fn)(struct PN532 *pn532);

/** @brief Frame and transmit command ID/parameters; no ACK or response is read. */
typedef esp_err_t (*pn532_write_command_fn)(struct PN532 *pn532, const uint8_t *command, uint8_t command_len);

/**
 * @brief Read raw PN532 bytes into buffer, reporting the actual count in bytes_read.
 * timeout is in RTOS ticks. A short positive read can succeed; the core checks
 * whether the requested ACK or frame portion was received completely.
 */
typedef esp_err_t (*pn532_read_response_fn)(struct PN532 *pn532, uint8_t *buffer, size_t buffer_size, size_t *bytes_read, TickType_t timeout);

/** @brief Release transport resources only; the core owns the allocation and mutex. */
typedef esp_err_t (*pn532_free_fn)(struct PN532 *pn532);

/** @brief State retained by the UART transport. */
typedef struct
{
    uart_port_t uart_port; /**< Port whose UART driver is owned by this handle. */
} PN532UartContext;

/** @brief Reserved context; I2C is not implemented. */
typedef struct
{
    /* Reserved for future I2C state. */
} PN532I2CContext;

/** @brief Reserved context; SPI is not implemented. */
typedef struct
{
    /* Reserved for future SPI state. */
} PN532SpiContext;

/** @brief Core-owned state behind the opaque public handle. */
typedef struct PN532
{
    uint8_t buffer[PN532_BUFFER_SIZE]; /**< Shared scratch space; overwritten by receive operations. */

    PN532Protocol protocol; /**< Active interface and matching context-union member. */
    union
    {
        PN532UartContext uart; /**< UART transport state. */
        PN532I2CContext i2c;   /**< Reserved I2C state. */
        PN532SpiContext spi;   /**< Reserved SPI state. */
    };

    SemaphoreHandle_t mutex; /**< Core-owned lock for complete start/command exchanges. */

    pn532_start_fn start;                 /**< Transport wake-up callback. */
    pn532_write_command_fn write_command; /**< Transport command-transmit callback. */
    pn532_read_response_fn read_response; /**< Transport raw-read callback. */
    pn532_free_fn free;                   /**< Transport-resource cleanup callback. */
} PN532;

/** @brief Initialize UART state and callbacks on an already allocated core handle. */
esp_err_t pn532_uart_init(PN532 *pn532, const PN532UartConfig *config);

/**
 * @brief Build a normal command frame, without any transport-specific prefix.
 * command includes the ID and parameters. The caller supplies a valid length
 * of 1-254 and frame capacity of command_len + PN532_FRAME_OVERHEAD bytes.
 * Returns the encoded frame length; performs no I/O.
 */
size_t pn532_command_framer(const uint8_t *command, size_t command_len, uint8_t *frame);

/**
 * @brief Validate a complete normal response and return a borrowed payload view.
 * The core has already checked the header. This helper checks total length,
 * trailer, data checksum, direction, and expected response ID.
 * On ESP_OK, payload aliases frame and excludes TFI and the response ID; it stays
 * valid only while frame is unchanged. On error, payload is NULL and its length
 * is zero. Returns ESP_FAIL for a PN532 error frame, otherwise
 * ESP_ERR_INVALID_RESPONSE for invalid response data.
 */
esp_err_t pn532_response_parser(const uint8_t *frame, size_t frame_len, uint8_t expected_command, const uint8_t **payload, size_t *payload_len);

#endif // PN532_INTERNAL_H
