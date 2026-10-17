#include "driver/uart.h"
#include "esp_rom_sys.h"
#include "pn532.h"
#include "pn532_internal.h"

#include "esp_log.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PN532_UART_BAUD_RATE 115200U

#define PN532_UART_START_TIMEOUT_MS 1000U
#define PN532_UART_WAKEUP_DELAY_US 5000U

#define PN532_UART_RX_BUF_SIZE 256U
#define PN532_UART_TX_BUF_SIZE 256U

#define PN532_UART_PREAMBLE_SIZE 5U

#define PN532_UART_TX_TIMEOUT_MS 1000U

#define UART_PORT(pn532) ((pn532)->uart.uart_port)

static const char *TAG = "pn532";

static const uint8_t pn532_uart_preamble[] = {0x55U, 0x55U, 0x00U, 0x00U, 0x00U};

/*
 * UART wake-up only; does not configure or verify the PN532.
 * UM0701-02, Rev. 02, §7.2.11, p. 99: raw 0x55 followed by
 * the wake-up interval. See §6.3.2.3, p. 50 for oscillator startup.
 */
static esp_err_t pn532_uart_start(PN532 *pn532)
{
    assert((pn532 != NULL) &&
           (pn532->protocol == PN532_UART_PROTOCOL));

    if (pn532->uart.uart_port >= UART_NUM_MAX)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t timeout_ticks = pdMS_TO_TICKS(PN532_UART_START_TIMEOUT_MS);

    esp_err_t status = uart_wait_tx_done(pn532->uart.uart_port, timeout_ticks);
    if (status != ESP_OK)
    {
        return status;
    }

    const uint8_t wakeup = PN532_WAKEUP;

    const int written = uart_write_bytes(pn532->uart.uart_port, &wakeup, sizeof(wakeup));
    if (written != (int)sizeof(wakeup))
    {
        return ESP_FAIL;
    }

    status = uart_wait_tx_done(pn532->uart.uart_port, timeout_ticks);
    if (status != ESP_OK)
    {
        return status;
    }

    esp_rom_delay_us(PN532_UART_WAKEUP_DELAY_US);

    return status;
}

static esp_err_t pn532_uart_write_command(PN532 *pn532, const uint8_t *command, uint8_t command_len)
{
    assert((pn532 != NULL) &&
           (command != NULL));

    if ((command_len == 0) ||
        (command_len > PN532_NORMAL_COMMAND_MAX_LEN))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[PN532_UART_PREAMBLE_SIZE + PN532_FRAME_OVERHEAD + command_len];
    (void)memcpy(cmd, pn532_uart_preamble, PN532_UART_PREAMBLE_SIZE);

    const size_t frame_len = pn532_command_framer(command, command_len, cmd + PN532_UART_PREAMBLE_SIZE);
    const size_t cmd_len = PN532_UART_PREAMBLE_SIZE + frame_len;

    const TickType_t tx_timeout_ticks = pdMS_TO_TICKS(PN532_UART_TX_TIMEOUT_MS);

    esp_err_t status = uart_wait_tx_done(pn532->uart.uart_port, tx_timeout_ticks);
    if (status != ESP_OK)
    {
        return status;
    }

    const int written = uart_write_bytes(pn532->uart.uart_port, cmd, cmd_len);
    if (written != (int)cmd_len)
    {
        return ESP_FAIL;
    }

    status = uart_wait_tx_done(pn532->uart.uart_port, tx_timeout_ticks);

    return status;
}

static esp_err_t pn532_uart_read_response(PN532 *pn532, uint8_t *buffer, size_t buffer_size, size_t *bytes_read, TickType_t timeout)
{
    assert((pn532 != NULL) &&
           (buffer != NULL) &&
           (bytes_read != NULL));

    *bytes_read = 0U;

    if ((buffer_size == 0U) ||
        (buffer_size > UINT32_MAX))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int read = uart_read_bytes(pn532->uart.uart_port, buffer, (uint32_t)buffer_size, timeout);
    if (read < 0)
    {
        return ESP_FAIL;
    }

    if (read == 0)
    {
        return ESP_ERR_TIMEOUT;
    }

    *bytes_read = (size_t)read;

    return ESP_OK;
}

static esp_err_t pn532_uart_free(PN532 *pn532)
{
    esp_err_t err = uart_driver_delete(UART_PORT(pn532));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_delete failed: %d", err);
        return err;
    }

    return ESP_OK;
}

esp_err_t pn532_uart_init(PN532 *pn532, const PN532UartConfig *config)
{
    assert((pn532 != NULL) &&
           (config != NULL));

    const uart_config_t uart_config = {
        .baud_rate = PN532_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t status = uart_is_driver_installed(config->uart_port) ? ESP_ERR_INVALID_STATE : ESP_OK;

    if (status == ESP_OK)
    {
        status = uart_param_config(config->uart_port, &uart_config);
    }

    if (status == ESP_OK)
    {
        status = uart_set_pin(config->uart_port, config->tx, config->rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    bool uart_driver_installed = false;
    if (status == ESP_OK)
    {
        status = uart_driver_install(config->uart_port, PN532_UART_RX_BUF_SIZE, PN532_UART_TX_BUF_SIZE, 0, NULL, 0);
        uart_driver_installed = (status == ESP_OK);
    }

    if (status == ESP_OK)
    {
        pn532->protocol = PN532_UART_PROTOCOL;
        pn532->uart.uart_port = config->uart_port;
        pn532->start = pn532_uart_start;
        pn532->write_command = pn532_uart_write_command;
        pn532->read_response = pn532_uart_read_response;
        pn532->free = pn532_uart_free;
    }
    else
    {
        if (uart_driver_installed)
        {
            (void)uart_driver_delete(config->uart_port);
        }
    }

    return status;
}
