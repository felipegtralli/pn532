#pragma once

#include <esp_err.h>

#include <driver/gpio.h>
#include <driver/uart.h>

#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00
#define PN532_HOSTTOPN532 0xD4

#define PN532_COMMAND_DIAGNOSE 0x00
#define PN532_COMMAND_GETFIRMWAREVERSION 0x02
#define PN532_COMMAND_GETGENERALSTATUS 0x04
#define PN532_COMMAND_READREGISTER 0x06
#define PN532_COMMAND_WRITEREGISTER 0x08
#define PN532_COMMAND_READGPIO 0x0C
#define PN532_COMMAND_WRITEGPIO 0x0E
#define PN532_COMMAND_SETSERIALBAUDRATE 0x10
#define PN532_COMMAND_SETPARAMETERS 0x12
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_POWERDOWN 0x16
#define PN532_COMMAND_RFCONFIGURATION 0x32
#define PN532_COMMAND_INJUMPFORDEP 0x56
#define PN532_COMMAND_INJUMPFORPSL 0x46
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INATR 0x50
#define PN532_COMMAND_INPSL 0x4E
#define PN532_COMMAND_INDATAEXCHANGE 0x40
#define PN532_COMMAND_INCOMMUNICATETHRU 0x42
#define PN532_COMMAND_INDESELECT 0x44
#define PN532_COMMAND_INRELEASE 0x52
#define PN532_COMMAND_INSELECT 0x54
#define PN532_COMMAND_INAUTOPOLL 0x60
#define PN532_COMMAND_TGINITASTARGET 0x8C
#define PN532_COMMAND_TGSETGENERALBYTES 0x92
#define PN532_COMMAND_TGGETDATA 0x86
#define PN532_COMMAND_TGSETDATA 0x8E
#define PN532_COMMAND_TGSETMETADATA 0x94
#define PN532_COMMAND_TGGETINITIATORCOMMAND 0x88
#define PN532_COMMAND_TGRESPONSETOINITIATOR 0x90
#define PN532_COMMAND_TGGETTARGETSTATUS 0x8A

#define PN532_WAKEUP 0x55

#define PN532_MIFARE_ISO14443A 0x00

/**
 * @brief PN532 protocol type
 * 
 */
typedef enum {
    PN532_UART_PROTOCOL,
    PN532_I2C_PROTOCOL,
    PN532_SPI_PROTOCOL,
    PN532_MAX_PROTOCOL,
} pn532_protocol_t;

/**
 * @brief PN532 handle type
 * 
 */
typedef struct pn532_t* pn532_handle_t;

/**
 * @brief PN532 uart configuration
 * 
 */
typedef struct {
    gpio_num_t tx; // UART TX pin
    gpio_num_t rx; // UART RX pin
    uart_port_t uart_port; // UART port number. UART_NUM_0 ~ (UART_NUM_MAX - 1)
    uint32_t baud_rate; // UART baud rate
} pn532_uart_config_t;

/**
 * @brief PN532 i2c configuration
 * 
 */
typedef struct {
    // todo 
} pn532_i2c_config_t;

/**
 * @brief PN532 spi configuration
 * 
 */
typedef struct {
    // todo
} pn532_spi_config_t;

/**
 * @brief PN532 configuration
 * 
 */
typedef struct {
    pn532_protocol_t protocol;
    union {
        pn532_uart_config_t uart;
        pn532_i2c_config_t i2c;
        pn532_spi_config_t spi;
    };
} pn532_config_t;

/**
 * @brief Initialize PN532 device.
 * 
 * Sets up the PN532 device with the given configuration.
 * (UART, I2C, or SPI) ** Only UART is implemented **
 * 
 * @param[out] pn532_handle Pointer to the PN532 handle.
 * @param[in] config Configuration settings for the PN532 device.
 * 
 * @return 
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if the configuration is invalid.
 *  - ESP_ERR_NO_MEM if memory allocation failed.
 *  - Other error codes from the protocol-specific initialization functions.
 */
esp_err_t pn532_init(pn532_handle_t* pn532_handle, const pn532_config_t* config);

/**
 * @brief Free PN532 device.
 * 
 * Frees the resources used by the PN532 device.
 * 
 * @param[in] pn532_handle PN532 handle.
 * 
 * @return 
 *  - ESP_OK on success.
 *  - ESP_ERR_INVALID_ARG if the handle is invalid.
 *  - Other error codes from the protocol-specific free function.
 */
esp_err_t pn532_free(pn532_handle_t pn532_handle);

/**
 * @brief Start PN532 device.
 * 
 * Sends a wakeup command to the PN532 and checks the acknowledgment.
 * 
 * @param[in] pn532_handle PN532 handle.
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the acknowledgment check failed.
 */
esp_err_t pn532_start(pn532_handle_t pn532_handle);

/**
 * @brief Send command to PN532 and check acknowledgment.
 * 
 * Writes a command to the PN532 and checks the acknowledgment.
 * 
 * @param[in] pn532_handle PN532 handle.
 * @param[in] command Pointer to the command buffer.
 * @param[in] command_len Length of the command buffer.
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle or command is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the acknowledgment is invalid.
 * - Other error codes from write and read functions.
 */
esp_err_t pn532_send_command_check_ack(pn532_handle_t pn532_handle, uint8_t* command, uint8_t command_len);

/**
 * @brief Get PN532 firmware version.
 * 
 * Sends a firmware version request to the PN532 and reads the version information.
 * 
 * @param[in] pn532_handle PN532 handle.
 * @param[out] version Pointer to the version buffer (buffer MUST have atleast 4 bytes).
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle or version is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the firmware version check or ackowledgment failed.
 */
esp_err_t pn532_get_firmware_version(pn532_handle_t pn532_handle, uint8_t* version);

/**
 * @brief Configure PN532 in SAM (Security Access Module) mode.
 * 
 * Configures the PN532 in SAM mode with IRQ and a timeout of 1s.
 * 
 * @param[in] pn532_handle PN532 handle.
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the SAM configuration check or ackowledgment failed.
 */
esp_err_t pn532_SAM_configuration(pn532_handle_t pn532_handle);

/**
 * @brief Set passive activation retries.
 * 
 * Sets the maximum number of retries for passive target activation. 0X00 means only one try, no retries. 0xFF means infinite retries.
 * 
 * @param[in] pn532_handle PN532 handle.
 * @param[in] max_retries Maximum number of retries.
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the command check or ackowledgment failed.
 */
esp_err_t pn532_set_passive_activation_retries(pn532_handle_t pn532_handle, uint8_t max_retries);

/**
 * @brief Read UID of passive target.
 * 
 * Detects a passive target and reads its UID
 * 
 * @param[in] pn532_handle PN532 handle.
 * @param[in] card_baud_rate Baud rate of the card (e.g., ISO14443A).
 * @param[out] uid Buffer to store the UID.
 * @param[out] uid_len Pointer to the UID length.
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle or UID buffer is invalid.
 * - ESP_ERR_INVALID_RESPONSE if target detection or acknowledgment failed.
 * - ESP_ERR_NOT_FOUND if no target was found.
 */
esp_err_t pn532_read_passive_target_id(pn532_handle_t pn532_handle, uint8_t card_baud_rate, uint8_t* uid, size_t* uid_len);

/**
 * @brief Read GPIO state.
 * 
 * Reads the state of the GPIO pins P3, P7, and I0.
 * Buffer[0] = P3, Buffer[1] = P7, Buffer[2] = I0.
 * 
 * @param[in] pn532_handle PN532 handle.
 * @param[out] gpio_state Buffer to store the GPIO state (Buffer must have atleast 3 bytes).
 * 
 * @return
 * - ESP_OK on success.
 * - ESP_ERR_INVALID_ARG if the handle or GPIO state buffer is invalid.
 * - ESP_ERR_INVALID_RESPONSE if the command check or ackowledgment failed.
 */
esp_err_t pn532_read_gpio(pn532_handle_t pn532_handle, uint8_t* gpio_state);