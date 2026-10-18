/**
 * @file pn532.h
 * @brief Public PN532 driver API for ESP-IDF.
 *
 * Public operations use the host interface selected at initialization.
 * Currently only UART is implemented; I2C and SPI are placeholders.
 *
 * Typical use: pn532_init(), pn532_start(), pn532_SAM_configuration(), then
 * command operations. Call pn532_free() when the handle is no longer needed.
 *
 * @section pn532_calling_rules Calling and timeout rules
 * Call from task context where blocking is allowed, not from an ISR. Operations
 * on one handle are serialized across the complete wake-up or command exchange.
 * Initialization and freeing are not protected against concurrent use: the
 * caller must coordinate the handle's lifetime and exclusive interface ownership.
 * Caller-provided buffers remain caller-owned and must stay valid for the call.
 * The driver does not retain their addresses after returning.
 *
 * Mutex acquisition uses a fixed 1000 ms timeout. Command wrappers use 1000 ms
 * for each receive operation; pn532_exec_command() lets the caller choose that
 * receive timeout. These are not whole-operation deadlines: separate lock,
 * receive, and transport-specific waits can accumulate. Millisecond values are
 * converted to RTOS ticks. A complete response does not wait for the entire
 * receive timeout.
 *
 * @section pn532_command_errors Common command errors and limitations
 * Command functions can return ESP_ERR_TIMEOUT for lock or communication waits,
 * ESP_ERR_INVALID_RESPONSE for an invalid ACK, frame, or command-specific payload,
 * ESP_ERR_NOT_SUPPORTED for an extended response frame, and ESP_ERR_INVALID_SIZE
 * for a response exceeding the driver's receive capacity or the caller's buffer.
 * ESP_FAIL can indicate a PN532 error frame, a transport failure, or failure to
 * release the mutex. Other ESP-IDF transport errors are propagated.
 *
 * The driver does not automatically retry, abort commands, or resynchronize the
 * link after errors. Timeouts, malformed headers, and unsupported frames can
 * leave unread bytes or an outstanding device command; a subsequent call is not
 * guaranteed to begin with a clean receive stream.
 *
 * Hardware field descriptions refer to NXP UM0701-02, PN532 User Manual,
 * Rev. 02, 5 November 2007 (firmware V1.6).
 */

#ifndef PN532_H
#define PN532_H

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

/**
 * @name Command identifiers
 * Values for command[0] in pn532_exec_command(). These are chip command IDs,
 * not a list of implemented wrappers or guaranteed operating modes. The caller
 * must supply the command-specific parameters and interpret its status/data.
 * The driver does not track interface, communication-parameter, or device-state
 * changes requested using these IDs.
 *
 * Reference: UM0701-02 Rev. 02, Section 7.1, Table 12, pp. 65-66.
 * @{
 */
#define PN532_COMMAND_DIAGNOSE 0x00           /**< Run diagnostic tests. */
#define PN532_COMMAND_GETFIRMWAREVERSION 0x02 /**< Read chip and firmware identification. */
#define PN532_COMMAND_GETGENERALSTATUS 0x04   /**< Read the PN532 operating status. */
#define PN532_COMMAND_READREGISTER 0x06       /**< Read internal registers. */
#define PN532_COMMAND_WRITEREGISTER 0x08      /**< Write internal registers. */
#define PN532_COMMAND_READGPIO 0x0C           /**< Read GPIO and interface-mode inputs. */
#define PN532_COMMAND_WRITEGPIO 0x0E          /**< Write GPIO outputs. */
#define PN532_COMMAND_SETSERIALBAUDRATE 0x10  /**< Change the chip's HSU baud rate. */
#define PN532_COMMAND_SETPARAMETERS 0x12      /**< Set chip communication parameters. */
#define PN532_COMMAND_SAMCONFIGURATION 0x14   /**< Select the SAM data-flow mode. */
#define PN532_COMMAND_POWERDOWN 0x16          /**< Enter a chip power-down state. */

#define PN532_COMMAND_RFCONFIGURATION 0x32 /**< Configure RF parameters. */

#define PN532_COMMAND_INDATAEXCHANGE 0x40      /**< Exchange data with a selected target. */
#define PN532_COMMAND_INCOMMUNICATETHRU 0x42   /**< Exchange RF data with a target. */
#define PN532_COMMAND_INDESELECT 0x44          /**< Deselect an initialized target. */
#define PN532_COMMAND_INJUMPFORPSL 0x46        /**< Initialize an NFC peer with parameter selection. */
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A /**< Discover and initialize passive targets. */
#define PN532_COMMAND_INPSL 0x4E               /**< Select communication parameters as initiator. */
#define PN532_COMMAND_INATR 0x50               /**< Perform attribute exchange as initiator. */
#define PN532_COMMAND_INRELEASE 0x52           /**< Release an initialized target. */
#define PN532_COMMAND_INSELECT 0x54            /**< Select an initialized target. */
#define PN532_COMMAND_INJUMPFORDEP 0x56        /**< Initialize an NFC-DEP peer. */
#define PN532_COMMAND_INAUTOPOLL 0x60          /**< Poll for targets using chip-side polling. */

#define PN532_COMMAND_TGGETDATA 0x86             /**< Receive data in target mode. */
#define PN532_COMMAND_TGGETINITIATORCOMMAND 0x88 /**< Receive an initiator command in target mode. */
#define PN532_COMMAND_TGGETTARGETSTATUS 0x8A     /**< Read target-mode status. */
#define PN532_COMMAND_TGINITASTARGET 0x8C        /**< Initialize the PN532 as a target. */
#define PN532_COMMAND_TGSETDATA 0x8E             /**< Send data in target mode. */
#define PN532_COMMAND_TGRESPONSETOINITIATOR 0x90 /**< Send a response to the initiator. */
#define PN532_COMMAND_TGSETGENERALBYTES 0x92     /**< Set NFC-DEP general bytes in target mode. */
#define PN532_COMMAND_TGSETMETADATA 0x94         /**< TgSetMetaData command. */
/** @} */

/**
 * @brief ISO/IEC 14443 Type A, 106 kbit/s selector for pn532_read_passive_target_id().
 *
 * Reference: UM0701-02 Rev. 02, Section 7.3.5, p. 115.
 */
#define PN532_MIFARE_ISO14443A 0x00

/**
 * @brief Host interface used to connect the ESP32 to the PN532.
 */
typedef enum
{
    PN532_UART_PROTOCOL, /**< UART (HSU), currently the only implemented interface. */
    PN532_I2C_PROTOCOL,  /**< Reserved I2C interface; initialization returns ESP_ERR_NOT_SUPPORTED. */
    PN532_SPI_PROTOCOL,  /**< Reserved SPI interface; initialization returns ESP_ERR_NOT_SUPPORTED. */
    PN532_MAX_PROTOCOL,  /**< Interface-count sentinel, not a valid configuration value. */
} PN532Protocol;

/**
 * @brief Opaque handle returned by pn532_init() and released by pn532_free().
 *
 * Copying a handle does not create another instance or extend its lifetime.
 * All copies become invalid after a successful pn532_free(). Set caller-held
 * copies to NULL as appropriate; pn532_free() cannot clear them itself.
 */
typedef struct PN532 *PN532Handle;

/**
 * @brief ESP-IDF UART resources used by the driver.
 *
 * The driver installs and owns the selected UART driver. Do not select a port
 * already installed or used by another component, including a UART console.
 * Baud rate is fixed at 115200, with 8N1 and no hardware flow control.
 */
typedef struct
{
    gpio_num_t tx;         /**< ESP32 TX GPIO; connect to the PN532 RX signal. */
    gpio_num_t rx;         /**< ESP32 RX GPIO; connect to the PN532 TX signal. */
    uart_port_t uart_port; /**< Available UART port in the range [UART_NUM_0, UART_NUM_MAX). */
} PN532UartConfig;

/**
 * @brief Reserved configuration placeholder; I2C is not implemented.
 */
typedef struct
{
    /* Reserved for future I2C settings. */
} PN532I2CConfig;

/**
 * @brief Reserved configuration placeholder; SPI is not implemented.
 */
typedef struct
{
    /* Reserved for future SPI settings. */
} PN532SpiConfig;

/**
 * @brief Initialization settings for one PN532 handle.
 *
 * Set protocol and the matching union member. The configuration is read during
 * pn532_init() and is not retained, so it may be local to the calling function.
 */
typedef struct
{
    PN532Protocol protocol; /**< Selects which interface-specific settings are used. */
    union
    {
        PN532UartConfig uart; /**< Settings for PN532_UART_PROTOCOL. */
        PN532I2CConfig i2c;   /**< Reserved settings for PN532_I2C_PROTOCOL. */
        PN532SpiConfig spi;   /**< Reserved settings for PN532_SPI_PROTOCOL. */
    };
} PN532Config;

/**
 * @brief Allocate a handle and initialize its host interface.
 *
 * Initializes the selected host interface and creates the transaction mutex.
 * This does not wake, configure, or communicate with the PN532. Interface
 * selection on the physical module must already match the requested protocol.
 *
 * On success, the caller owns the returned handle. On failure, *pn532_handle
 * is unchanged; initialize the caller's handle to NULL before the call. Do not
 * overwrite an existing live handle with another initialization.
 *
 * @param[out] pn532_handle Non-NULL pointer receiving the handle on success.
 * @param[in] config Non-NULL configuration; no pointer to it is retained.
 *
 * @retval ESP_OK Host interface initialized; handle returned.
 * @retval ESP_ERR_INVALID_ARG A required pointer is NULL or the protocol value is invalid.
 * @retval ESP_ERR_NO_MEM Handle, mutex, or transport allocation failed.
 * @retval ESP_ERR_NOT_SUPPORTED The selected host interface is not implemented.
 * @retval ESP_ERR_INVALID_STATE The selected host-interface resource is already in use.
 * @return Other transport initialization errors are propagated.
 */
esp_err_t pn532_init(PN532Handle *pn532_handle, const PN532Config *config);

/**
 * @brief Release the host-interface resources and handle.
 *
 * Stop all users of the handle before calling, including tasks waiting for its
 * mutex. This function does not take that mutex and cannot safely race another
 * API call. It does not send a power-down or reset command to the PN532.
 *
 * A successful call invalidates all copies of the handle. If transport cleanup
 * fails, the handle allocation and mutex are retained and an error is returned.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 *
 * @retval ESP_OK Host resources and handle released.
 * @retval ESP_ERR_INVALID_ARG pn532_handle is NULL.
 * @return Other transport cleanup errors are propagated.
 */
esp_err_t pn532_free(PN532Handle pn532_handle);

/**
 * @brief Wake the PN532 using the configured host interface.
 *
 * Runs the selected interface's wake-up operation. ESP_OK means the host-side
 * operation completed, not that the PN532 answered or is configured.
 * SAM configuration and communication verification are separate operations.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 *
 * @retval ESP_OK The interface-specific wake-up operation completed.
 * @retval ESP_ERR_INVALID_ARG pn532_handle is NULL.
 * @retval ESP_ERR_INVALID_STATE The configured transport state is invalid.
 * @retval ESP_ERR_TIMEOUT Mutex acquisition or a transport operation timed out.
 * @retval ESP_FAIL The transport operation or mutex release failed.
 * @return Other transport errors are propagated.
 */
esp_err_t pn532_start(PN532Handle pn532_handle);

/**
 * @brief Execute a command, validate its ACK and response, and optionally copy its payload.
 *
 * command contains the command ID followed by its parameters, without TFI,
 * framing, or checksums. The driver adds those fields and expects a response ID
 * equal to command[0] + 1. Returned payload bytes exclude framing, TFI, and the
 * response ID. Any command-specific status byte remains part of the payload:
 * the caller must interpret it; ESP_OK alone does not validate that status.
 *
 * Only normal frames are supported. The current receive capacity accepts at
 * most 247 payload bytes (256 bytes including the complete response frame).
 *
 * Set response to NULL and response_size to zero to discard the payload. The
 * ACK and complete response are still consumed and validated; response_len is
 * required even when discarding. A too-small destination is not partially
 * filled, and the valid response has already been consumed when that size error
 * is reported. Increasing response_size does not increase the receive limit.
 *
 * timeout is passed to each transport receive operation, not used as a whole-call
 * deadline. Lock acquisition and other transport waits are separate. Values
 * are converted with pdMS_TO_TICKS(); very small values can become zero ticks,
 * and large values must fit the SDK's tick-conversion arithmetic. A zero receive
 * timeout does not make the whole call nonblocking.
 *
 * References: UM0701-02 Rev. 02, Sections 6.2.1.1-2, pp. 28-29, and 7.1, p. 65.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 * @param[in] command Non-NULL command and parameter bytes.
 * @param[in] command_len Number of command bytes, including the command ID; 1-254.
 * @param[out] response Optional caller-owned payload buffer.
 * @param[in] response_size Capacity of response in bytes; must be zero if response is NULL.
 * @param[out] response_len Non-NULL payload-length output. Initially set to zero;
 *                         updated after frame validation, including when a
 *                         valid payload exceeds response_size. Remains zero if
 *                         the frame exceeds the driver's receive capacity.
 * @param[in] timeout Receive wait in milliseconds, converted to RTOS ticks.
 *
 * @retval ESP_OK ACK and response validated; payload copied or discarded.
 * @retval ESP_ERR_INVALID_ARG A required pointer, command length, or NULL-buffer size is invalid.
 * @retval ESP_ERR_INVALID_SIZE Destination or driver receive capacity is too small.
 * @retval ESP_ERR_INVALID_RESPONSE ACK, frame, or response ID is invalid.
 * @retval ESP_ERR_NOT_SUPPORTED The response uses an extended frame.
 * @retval ESP_ERR_TIMEOUT Mutex acquisition or communication timed out.
 * @retval ESP_FAIL PN532 error frame, transport failure, or mutex-release failure.
 * @return Other ESP-IDF transport errors are propagated. See @ref pn532_command_errors.
 */
esp_err_t pn532_exec_command(PN532Handle pn532_handle,
                             const uint8_t *command,
                             uint8_t command_len,
                             uint8_t *response,
                             size_t response_size,
                             size_t *response_len,
                             uint32_t timeout);

/**
 * @brief Read the four-byte chip and firmware identification payload.
 *
 * On success, version[0] is the chip ID, version[1] the firmware version,
 * version[2] the revision, and version[3] the feature-support mask. Its bits
 * 0, 1, and 2 indicate ISO/IEC 14443 Type A, Type B, and ISO/IEC 18092 support.
 * This reports chip capabilities, not additional features implemented by this
 * driver. No specific chip ID or firmware version is required by this function.
 *
 * A buffer smaller than four bytes is rejected before communication. On other
 * failures the output may have been modified; use it only after ESP_OK.
 * Uses the fixed command-wrapper timeouts in @ref pn532_calling_rules.
 *
 * Reference: UM0701-02 Rev. 02, Section 7.2.2, p. 73.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 * @param[out] version Non-NULL caller-owned buffer receiving four bytes.
 * @param[in] version_size Capacity of version in bytes; at least four.
 *
 * @retval ESP_OK A four-byte identification payload was received.
 * @retval ESP_ERR_INVALID_ARG pn532_handle or version is NULL.
 * @retval ESP_ERR_INVALID_SIZE version_size is too small or the response exceeds capacity.
 * @retval ESP_ERR_INVALID_RESPONSE The ACK, frame, or payload length is invalid.
 * @return Other common command errors; see @ref pn532_command_errors.
 */
esp_err_t pn532_get_firmware_version(PN532Handle pn532_handle, uint8_t *version, size_t version_size);

/**
 * @brief Select SAM Normal mode, without using a companion Security Access Module.
 *
 * Sends SAMConfiguration with Mode=0x01 and IRQ=0x01. The command's Timeout
 * byte is 0x14, but that device parameter is ignored in Normal mode; it does not
 * configure the driver's host-side waits. IRQ output is enabled on the chip,
 * but this driver does not use an IRQ input. Virtual, Wired, and Dual Card modes
 * are not selectable through this wrapper.
 *
 * Uses the fixed command-wrapper timeouts in @ref pn532_calling_rules.
 * Reference: UM0701-02 Rev. 02, Section 7.2.10, p. 89.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 *
 * @retval ESP_OK ACK and empty SAMConfiguration response validated.
 * @retval ESP_ERR_INVALID_ARG pn532_handle is NULL.
 * @retval ESP_ERR_INVALID_RESPONSE The ACK, frame, or expected empty payload is invalid.
 * @return Other common command errors; see @ref pn532_command_errors.
 */
esp_err_t pn532_SAM_configuration(PN532Handle pn532_handle);

/**
 * @brief Set the PN532's passive-target activation retry count.
 *
 * Sends RFConfiguration item 0x05 with MxRtyPassiveActivation=max_retries.
 * The same command also sets MxRtyATR=0xFF and MxRtyPSL=0x01, their chip defaults.
 * This is a device setting, not a driver-side retry loop or host timeout.
 *
 * With 0xFF, a later target-discovery command can remain active beyond the
 * driver's receive timeout. A host timeout does not automatically abort it.
 * Choose a finite value for applications that repeatedly poll for a tag.
 * Uses the fixed command-wrapper timeouts in @ref pn532_calling_rules.
 *
 * Reference: UM0701-02 Rev. 02, Section 7.3.1, p. 103.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 * @param[in] max_retries 0 means one attempt without retries; 1-254 select a
 *                        finite retry count; 0xFF retries indefinitely.
 *
 * @retval ESP_OK ACK and empty RFConfiguration response validated.
 * @retval ESP_ERR_INVALID_ARG pn532_handle is NULL.
 * @retval ESP_ERR_INVALID_RESPONSE The ACK, frame, or expected empty payload is invalid.
 * @return Other common command errors; see @ref pn532_command_errors.
 */
esp_err_t pn532_set_passive_activation_retries(PN532Handle pn532_handle, uint8_t max_retries);

/**
 * @brief Discover one passive Type A target and copy its UID.
 *
 * Requests at most one target. This wrapper supports only the
 * PN532_MIFARE_ISO14443A selector and accepts UIDs of 4, 7, or 10 bytes. UID bytes
 * are copied in the order received; no integer or byte-order conversion occurs.
 *
 * Set *uid_len to the destination capacity before EVERY call, for example
 * sizeof(uid) for a local uint8_t uid[10]. On success it becomes the UID length.
 * If capacity is insufficient, it reports the required length without copying
 * any UID bytes. If no target is found, it becomes zero and uid is unchanged.
 * It is reset to zero before other validation when uid_len is non-NULL; once a
 * valid UID is parsed, its required length remains available on a size error.
 *
 * The passive activation retry setting controls chip-side discovery attempts;
 * a host timeout does not mean that no target exists. For polling applications,
 * use pn532_set_passive_activation_retries() with a finite retry count.
 * Uses the fixed command-wrapper timeouts in @ref pn532_calling_rules.
 *
 * Reference: UM0701-02 Rev. 02, Section 7.3.5, pp. 115-118.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 * @param[in] card_baud_rate Discovery selector; only PN532_MIFARE_ISO14443A is supported.
 * @param[out] uid Non-NULL caller-owned UID buffer; ten bytes fits all accepted UID sizes.
 * @param[in,out] uid_len Non-NULL pointer: input capacity, output actual or required length.
 *
 * @retval ESP_OK A target was found and its UID copied.
 * @retval ESP_ERR_NOT_FOUND A valid response reports no target; *uid_len is zero.
 * @retval ESP_ERR_INVALID_ARG pn532_handle, uid, or uid_len is NULL.
 * @retval ESP_ERR_NOT_SUPPORTED The selector or an extended response frame is unsupported.
 * @retval ESP_ERR_INVALID_SIZE UID capacity is insufficient, or the response exceeds receive capacity.
 * @retval ESP_ERR_INVALID_RESPONSE The ACK, frame, target layout, or UID length is invalid.
 * @return Other common command errors; see @ref pn532_command_errors.
 */
esp_err_t pn532_read_passive_target_id(PN532Handle pn532_handle, uint8_t card_baud_rate, uint8_t *uid, size_t *uid_len);

/**
 * @brief Read GPIO ports P3 and P7 and the I0/I1 interface-mode inputs.
 *
 * On success, gpio_state[0] is P3, gpio_state[1] is P7, and gpio_state[2] is I0I1.
 * The payload is copied only after its three-byte length is validated. A buffer
 * smaller than three bytes is rejected before communication without modifying it.
 * Uses the fixed command-wrapper timeouts in @ref pn532_calling_rules.
 *
 * Reference: UM0701-02 Rev. 02, Section 7.2.6, p. 79.
 *
 * @param[in] pn532_handle Live handle returned by pn532_init().
 * @param[out] gpio_state Non-NULL caller-owned buffer receiving three bytes.
 * @param[in] gpio_size Capacity of gpio_state in bytes; at least three.
 *
 * @retval ESP_OK A three-byte GPIO payload was received and copied.
 * @retval ESP_ERR_INVALID_ARG pn532_handle or gpio_state is NULL.
 * @retval ESP_ERR_INVALID_SIZE gpio_size is too small or the response exceeds capacity.
 * @retval ESP_ERR_INVALID_RESPONSE The ACK, frame, or payload length is invalid.
 * @return Other common command errors; see @ref pn532_command_errors.
 */
esp_err_t pn532_read_gpio(PN532Handle pn532_handle, uint8_t *gpio_state, size_t gpio_size);

#endif // PN532_H
