#include "pn532.h"
#include "pn532_internal.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PN532_MAX_CARDS 1
#define ACK_OFFSET 6

#define PN532_DEFAULT_TIMEOUT 100

static const char *TAG = "pn532-driver";

static uint8_t pn532_ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static uint8_t pn532_firmwareversion[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5};

esp_err_t pn532_init(PN532Handle *pn532_handle, const PN532Config *config)
{
    esp_err_t status = ESP_OK;
    PN532 *pn532 = NULL;

    if ((pn532_handle == NULL) ||
        (config == NULL))
    {
        status = ESP_ERR_INVALID_ARG;
    }
    else
    {
        pn532 = malloc(sizeof(PN532));
        if (pn532 == NULL)
        {
            status = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG, "failed to allocate memory for PN532 handle");
        }
        else
        {
            memset(pn532, 0, sizeof(PN532));
        }
    }

    if (status == ESP_OK)
    {
        switch (config->protocol)
        {
        case PN532_UART_PROTOCOL:
            status = pn532_uart_init(pn532, &config->uart);
            if (status != ESP_OK)
            {
                ESP_LOGE(TAG, "pn532_uart_init failed: %s", esp_err_to_name(status));
            }
            break;

        case PN532_I2C_PROTOCOL:
        case PN532_SPI_PROTOCOL:
            status = ESP_ERR_NOT_SUPPORTED;
            break;

        default:
            status = ESP_ERR_INVALID_ARG;
            break;
        }
    }

    if (status == ESP_OK)
    {
        *pn532_handle = pn532;
        ESP_LOGI(TAG, "PN532 initialized");
    }
    else
    {
        if (pn532 != NULL)
        {
            free(pn532);
            pn532 = NULL;
        }
    }

    return status;
}

esp_err_t pn532_free(PN532Handle pn532_handle)
{
    esp_err_t status = ESP_OK;
    PN532 *pn532 = NULL;

    if (pn532_handle == NULL)
    {
        status = ESP_ERR_INVALID_ARG;
    }
    else
    {
        pn532 = pn532_handle;
    }

    if (status == ESP_OK)
    {
        status = pn532->free(pn532);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG, "PN532 free failed: %s", esp_err_to_name(status));
        }
        else
        {
            free(pn532);
            pn532 = NULL;
        }
    }

    return status;
}

esp_err_t pn532_start(PN532Handle pn532_handle)
{
    esp_err_t status = ESP_OK;
    PN532 *pn532 = NULL;

    if (pn532_handle == NULL)
    {
        status = ESP_ERR_INVALID_ARG;
    }
    else
    {
        pn532 = pn532_handle;
    }

    if (status == ESP_OK)
    {
        status = pn532->start(pn532);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG, "PN532 start failed: %s", esp_err_to_name(status));
        }
        else
        {
            ESP_LOGI(TAG, "PN532 started");
        }
    }

    return status;
}

esp_err_t pn532_send_command_check_ack(PN532Handle pn532_handle, uint8_t *command, uint8_t command_len, uint32_t timeout)
{
    if (!pn532_handle || !command)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    vTaskDelay(20 / portTICK_PERIOD_MS);
    esp_err_t err = pn532->write_command(pn532, command, command_len);
    if (err != ESP_OK)
    {
        return err;
    }

#ifdef PN532_DEBUG
    ESP_LOGD(TAG, "reading ack:");
#endif

    vTaskDelay(20 / portTICK_PERIOD_MS);
    err = pn532->read_response(pn532, (timeout / portTICK_PERIOD_MS));
    if (err != ESP_OK)
    {
        return err;
    }

    if (memcmp(pn532->buffer, pn532_ack, sizeof(pn532_ack)) != 0)
    {
        ESP_LOGE(TAG, "failed to check ack");
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t pn532_get_firmware_version(PN532Handle pn532_handle, uint8_t *version)
{
    if (!pn532_handle || !version)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    esp_err_t err =
        pn532_send_command_check_ack(pn532, (uint8_t[]){PN532_COMMAND_GETFIRMWAREVERSION}, 1, PN532_DEFAULT_TIMEOUT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to get firmware version");
        return err;
    }

    uint8_t response[11];
    size_t len = sizeof(response);
    memcpy(response, (pn532->buffer + ACK_OFFSET), len);

    if (memcmp(response, pn532_firmwareversion, sizeof(pn532_firmwareversion)) != 0)
    {
        ESP_LOGE(TAG, "failed to check firmware version");
        return ESP_ERR_INVALID_RESPONSE;
    }

    version[0] = response[7];  // IC
    version[1] = response[8];  // firmware version
    version[2] = response[9];  // firmware revision
    version[3] = response[10]; // support

    return ESP_OK;
}

esp_err_t pn532_SAM_configuration(PN532Handle pn532_handle)
{
    if (!pn532_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_SAMCONFIGURATION,
        0x01, // normal mode
        0x14, // timeout 50ms * 20 = 1s
        0x01, // use IRQ pin
    };
    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command), PN532_DEFAULT_TIMEOUT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to configure SAM");
        return err;
    }

    uint8_t response[8];
    size_t len = sizeof(response);
    memcpy(response, (pn532->buffer + ACK_OFFSET), len);

    if (response[6] != 0x15)
    {
        ESP_LOGE(TAG, "failed to check SAM configuration");
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t pn532_set_passive_activation_retries(PN532Handle pn532_handle, uint8_t max_retries)
{
    if (!pn532_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_RFCONFIGURATION,
        0x05, // configuration item
        0xFF, // MxRtyATR (default 0xFF)
        0x01, // MxRtyPSL (default 0x01)
        max_retries,
    };

#ifdef PN532_DEBUG
    ESP_LOGD(TAG, "setting passive activation retries: %02X", max_retries);
#endif

    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command), PN532_DEFAULT_TIMEOUT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to set passive activation retries");
        return err;
    }

    return ESP_OK;
}

esp_err_t pn532_read_passive_target_id(PN532Handle pn532_handle, uint8_t card_baud_rate, uint8_t *uid, size_t *uid_len)
{
    if (!pn532_handle || !uid || !uid_len)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_INLISTPASSIVETARGET,
        PN532_MAX_CARDS,
        card_baud_rate,
    };
    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command), PN532_DEFAULT_TIMEOUT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to read passive target id");
        return err;
    }

    uint8_t response[20];
    size_t len = sizeof(response);
    memcpy(response, (pn532->buffer + ACK_OFFSET), len);

    if (!response[7])
    {
        ESP_LOGW(TAG, "no card detected");
        return ESP_ERR_NOT_FOUND;
    }

    *uid_len = response[12];
    for (size_t i = 0; i < *uid_len; i++)
    {
        uid[i] = response[13 + i];
    }

    return ESP_OK;
}

esp_err_t pn532_read_gpio(PN532Handle pn532_handle, uint8_t *gpio_state)
{
    if (!pn532_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_READGPIO,
    };
    size_t len = sizeof(command);

    esp_err_t err = pn532_send_command_check_ack(pn532, command, len, PN532_DEFAULT_TIMEOUT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to read GPIO");
        return err;
    }

    uint8_t response[11];
    len = sizeof(response);
    memcpy(response, (pn532->buffer + ACK_OFFSET), len);

    gpio_state[0] = response[7]; // P3
    gpio_state[1] = response[8]; // P7
    gpio_state[2] = response[9]; // I0

    return ESP_OK;
}
