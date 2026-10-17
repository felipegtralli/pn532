#include "pn532.h"
#include "esp_err.h"
#include "pn532_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"

#define PN532_MAX_TARGETS 1U

#define PN532_VERSION_SIZE 4U

#define PN532_VERSION_RESPONSE_SIZE 13U
#define PN532_SAM_RESPONSE_SIZE 9U
#define PN532_RF_CONFIGURATION_RESPONSE_SIZE 9U

#define PN532_TYPE_A_TARGET_COUNT_OFFSET 0U
#define PN532_TYPE_A_TARGET_NUMBER_OFFSET 1U
#define PN532_TYPE_A_UID_LENGTH_OFFSET 5U
#define PN532_TYPE_A_UID_OFFSET 6U

#define PN532_TYPE_A_TARGET_COUNT_SIZE 1U
#define PN532_TYPE_A_TARGET_HEADER_SIZE 6U
#define PN532_TYPE_A_NO_TARGETS 0U
#define PN532_TYPE_A_EXPECTED_TARGET_COUNT 1U
#define PN532_TYPE_A_FIRST_TARGET_NUMBER 1U

#define PN532_TYPE_A_UID_SINGLE_SIZE 4U
#define PN532_TYPE_A_UID_DOUBLE_SIZE 7U
#define PN532_TYPE_A_UID_TRIPLE_SIZE 10U

#define PN532_DEFAULT_TIMEOUT 1000U
#define PN532_MUTEX_TIMEOUT_MS 1000U

static const char *TAG = "pn532-driver";

static const uint8_t PN532_ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

static esp_err_t pn532_read_check_ack(PN532 *pn532, size_t *bytes_read, TickType_t timeout)
{
    assert((pn532 != NULL) &&
           (bytes_read != NULL));

    esp_err_t status = pn532->read_response(pn532, pn532->buffer, sizeof(PN532_ACK), bytes_read, timeout);
    if (status != ESP_OK)
    {
        return status;
    }

    if (*bytes_read != sizeof(PN532_ACK))
    {
        return ESP_ERR_TIMEOUT;
    }

    const int cmp = memcmp(pn532->buffer, PN532_ACK, sizeof(PN532_ACK));

    return (cmp == 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t pn532_recv_response(PN532 *pn532, uint8_t expected_command, size_t read_size, uint8_t *response, size_t response_size, size_t *response_len, TickType_t timeout)
{
    assert((pn532 != NULL) &&
           (response_len != NULL));

    *response_len = 0U;

    size_t bytes_read = 0U;

    if ((read_size == 0U) ||
        (read_size > sizeof(pn532->buffer)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t status = pn532_read_check_ack(pn532, &bytes_read, timeout);
    if (status != ESP_OK)
    {
        return status;
    }

    status = pn532->read_response(pn532, pn532->buffer, read_size, &bytes_read, timeout);
    if (status != ESP_OK)
    {
        return status;
    }

    const uint8_t *payload = NULL;
    size_t payload_len = 0U;

    status = pn532_response_parser(pn532->buffer, bytes_read, expected_command, &payload, &payload_len);
    if (status != ESP_OK)
    {
        return status;
    }

    *response_len = payload_len;
    if (response != NULL)
    {
        if (response_size < payload_len)
        {
            return ESP_ERR_INVALID_SIZE;
        }
        else
        {
            (void)memcpy(response, payload, payload_len);
        }
    }

    return ESP_OK;
}

/* UM0701-02 Rev. 02, §7.3.5, pp. 115–118: Type A target data and cascaded UID layout. */
static esp_err_t pn532_parse_type_a_uid(const uint8_t *response, size_t response_len, uint8_t *uid, size_t uid_size, size_t *uid_len)
{
    assert((response != NULL) &&
           (uid != NULL) &&
           (uid_len != NULL));

    *uid_len = 0U;

    if (response_len < PN532_TYPE_A_TARGET_COUNT_SIZE)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (response[PN532_TYPE_A_TARGET_COUNT_OFFSET] == PN532_TYPE_A_NO_TARGETS)
    {
        return ESP_ERR_NOT_FOUND;
    }

    if (response_len < PN532_TYPE_A_TARGET_HEADER_SIZE)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if ((response[PN532_TYPE_A_TARGET_COUNT_OFFSET] != PN532_TYPE_A_EXPECTED_TARGET_COUNT) ||
        (response[PN532_TYPE_A_TARGET_NUMBER_OFFSET] != PN532_TYPE_A_FIRST_TARGET_NUMBER))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t uid_length = response[PN532_TYPE_A_UID_LENGTH_OFFSET];
    if ((uid_length != PN532_TYPE_A_UID_SINGLE_SIZE) &&
        (uid_length != PN532_TYPE_A_UID_DOUBLE_SIZE) &&
        (uid_length != PN532_TYPE_A_UID_TRIPLE_SIZE))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (uid_length > (response_len - PN532_TYPE_A_UID_OFFSET))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *uid_len = uid_length;
    if (uid_size < uid_length)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    (void)memcpy(uid, &response[PN532_TYPE_A_UID_OFFSET], uid_length);

    return ESP_OK;
}

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

    bool mutex_created = false;
    if (status == ESP_OK)
    {
        pn532->mutex = xSemaphoreCreateMutex();
        if (pn532->mutex == NULL)
        {
            status = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG, "failed to create mutex for PN532 handle");
        }
        else
        {
            mutex_created = true;
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
        if (mutex_created)
        {
            vSemaphoreDelete(pn532->mutex);
            pn532->mutex = NULL;
        }

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
            vSemaphoreDelete(pn532->mutex);
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

    BaseType_t mutex_res = pdFALSE;
    if (status == ESP_OK)
    {
        mutex_res = xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
        if (mutex_res != pdTRUE)
        {
            status = ESP_ERR_TIMEOUT;
        }
    }

    if (mutex_res == pdTRUE)
    {
        status = pn532->start(pn532);
    }

    if (mutex_res == pdTRUE)
    {
        mutex_res = xSemaphoreGive(pn532->mutex);
        if ((mutex_res != pdTRUE) &&
            (status == ESP_OK))
        {
            status = ESP_FAIL;
        }
    }

    if (status == ESP_OK)
    {
        ESP_LOGI(TAG, "PN532 started");
    }
    else
    {
        ESP_LOGE(TAG, "PN532 start failed: %s", esp_err_to_name(status));
    }

    return status;
}

esp_err_t pn532_exec_command(PN532Handle pn532_handle,
                             const uint8_t *command,
                             uint8_t command_len,
                             uint8_t *response,
                             size_t response_size,
                             size_t *response_len,
                             uint32_t timeout)
{
    if (response_len == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *response_len = 0U;

    if ((pn532_handle == NULL) ||
        (command == NULL) ||
        (command_len == 0U) ||
        (command_len > PN532_NORMAL_COMMAND_MAX_LEN) ||
        ((response == NULL) && (response_size != 0U)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t mutex_res = xSemaphoreTake(pn532_handle->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
    if (mutex_res != pdTRUE)
    {
        ESP_LOGE(TAG, "PN532 command 0x%02X failed to take mutex: %s", (unsigned int)command[0], esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }

    PN532 *pn532 = pn532_handle;

    esp_err_t status = pn532->write_command(pn532_handle, command, command_len);
    if (status == ESP_OK)
    {
        const uint8_t expected_command = command[0] + 1U;
        status = pn532_recv_response(pn532, expected_command, sizeof(pn532->buffer), response, response_size, response_len, pdMS_TO_TICKS(timeout));
    }

    mutex_res = xSemaphoreGive(pn532_handle->mutex);
    if ((mutex_res != pdTRUE) &&
        (status == ESP_OK))
    {
        status = ESP_FAIL;
    }

    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "PN532 command 0x%02X failed: %s", (unsigned int)command[0], esp_err_to_name(status));
    }

    return status;
}

esp_err_t pn532_get_firmware_version(PN532Handle pn532_handle, uint8_t *version_buffer, size_t version_size)
{
    if ((pn532_handle == NULL) ||
        (version_buffer == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (version_size < PN532_VERSION_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    PN532 *pn532 = pn532_handle;

    const uint8_t firmware_command[] = {PN532_COMMAND_GETFIRMWAREVERSION};

    BaseType_t mutex_res = xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
    if (mutex_res != pdTRUE)
    {
        ESP_LOGE(TAG, "PN532 get firmware version failed to take mutex: %s", esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t status = pn532->write_command(pn532, firmware_command, sizeof(firmware_command));
    if (status == ESP_OK)
    {
        size_t response_len = 0U;
        const uint8_t expected_command = PN532_COMMAND_GETFIRMWAREVERSION + 1U;

        status = pn532_recv_response(pn532, expected_command, PN532_VERSION_RESPONSE_SIZE, version_buffer, version_size, &response_len, pdMS_TO_TICKS(PN532_DEFAULT_TIMEOUT));
        if (status == ESP_OK)
        {
            if (response_len != PN532_VERSION_SIZE)
            {
                status = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    mutex_res = xSemaphoreGive(pn532->mutex);
    if ((mutex_res != pdTRUE) &&
        (status == ESP_OK))
    {
        status = ESP_FAIL;
    }

    return status;
}

esp_err_t pn532_SAM_configuration(PN532Handle pn532_handle)
{
    if (pn532_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    const uint8_t sam_command[] = {
        PN532_COMMAND_SAMCONFIGURATION,
        0x01, /* normal mode */
        0x14, /* ignored in normal mode */
        0x01, /* irq enabled*/
    };

    BaseType_t mutex_res = xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
    if (mutex_res != pdTRUE)
    {
        ESP_LOGE(TAG, "PN532 SAM configuration failed to take mutex: %s", esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t status = pn532->write_command(pn532, sam_command, sizeof(sam_command));
    if (status == ESP_OK)
    {
        size_t response_len = 0U;
        const uint8_t expected_command = PN532_COMMAND_SAMCONFIGURATION + 1U;

        status = pn532_recv_response(pn532, expected_command, PN532_SAM_RESPONSE_SIZE, NULL, 0U, &response_len, pdMS_TO_TICKS(PN532_DEFAULT_TIMEOUT));
        if (status == ESP_OK)
        {
            if (response_len != 0U)
            {
                status = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    mutex_res = xSemaphoreGive(pn532->mutex);
    if ((mutex_res != pdTRUE) &&
        (status == ESP_OK))
    {
        status = ESP_FAIL;
    }

    return status;
}

esp_err_t pn532_set_passive_activation_retries(PN532Handle pn532_handle, uint8_t max_retries)
{
    if (!pn532_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    PN532 *pn532 = pn532_handle;

    const uint8_t rf_config_command[] = {
        PN532_COMMAND_RFCONFIGURATION,
        0x05, /* maxretries config */
        0xFF, /* MxRtyATR default */
        0x01, /* MxRtyPSL default */
        max_retries,
    };

    BaseType_t mutex_res = xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
    if (mutex_res != pdTRUE)
    {
        ESP_LOGE(TAG, "PN532 set passive activation retries failed to take mutex: %s", esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t status = pn532->write_command(pn532, rf_config_command, sizeof(rf_config_command));
    if (status == ESP_OK)
    {
        size_t response_len = 0U;
        const uint8_t expected_command = PN532_COMMAND_RFCONFIGURATION + 1U;

        status = pn532_recv_response(pn532, expected_command, PN532_RF_CONFIGURATION_RESPONSE_SIZE, NULL, 0U, &response_len, pdMS_TO_TICKS(PN532_DEFAULT_TIMEOUT));
        if (status == ESP_OK)
        {
            if (response_len != 0U)
            {
                status = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    mutex_res = xSemaphoreGive(pn532->mutex);
    if ((mutex_res != pdTRUE) &&
        (status == ESP_OK))
    {
        status = ESP_FAIL;
    }

    return status;
}

esp_err_t pn532_read_passive_target_id(PN532Handle pn532_handle, uint8_t card_baud_rate, uint8_t *uid, size_t *uid_len)
{
    if (uid_len == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t uid_size = *uid_len;
    *uid_len = 0U;

    if ((pn532_handle == NULL) ||
        (uid == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (card_baud_rate != PN532_MIFARE_ISO14443A)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    PN532 *pn532 = pn532_handle;

    const uint8_t list_passive_target_command[] = {
        PN532_COMMAND_INLISTPASSIVETARGET,
        PN532_MAX_TARGETS,
        card_baud_rate,
    };

    BaseType_t mutex_res = xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(PN532_MUTEX_TIMEOUT_MS));
    if (mutex_res != pdTRUE)
    {
        ESP_LOGE(TAG, "PN532 read passive target id failed to take mutex: %s", esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t status = pn532->write_command(pn532, list_passive_target_command, sizeof(list_passive_target_command));
    if (status == ESP_OK)
    {
        uint8_t response[sizeof(pn532->buffer)];
        size_t response_len = 0U;
        const uint8_t expected_command = PN532_COMMAND_INLISTPASSIVETARGET + 1U;

        status = pn532_recv_response(pn532, expected_command, sizeof(response), response, sizeof(response), &response_len, pdMS_TO_TICKS(PN532_DEFAULT_TIMEOUT));
        if (status == ESP_OK)
        {
            status = pn532_parse_type_a_uid(response, response_len, uid, uid_size, uid_len);
        }
    }

    mutex_res = xSemaphoreGive(pn532->mutex);
    if ((mutex_res != pdTRUE) &&
        (status == ESP_OK))
    {
        return ESP_FAIL;
    }

    return status;
}

// esp_err_t pn532_read_gpio(PN532Handle pn532_handle, uint8_t *gpio_state)
// {
//     if (!pn532_handle)
//     {
//         return ESP_ERR_INVALID_ARG;
//     }

//     PN532 *pn532 = pn532_handle;

//     uint8_t command[] = {
//         PN532_COMMAND_READGPIO,
//     };
//     size_t len = sizeof(command);

//     esp_err_t err = pn532_send_command_check_ack(pn532, command, len, PN532_DEFAULT_TIMEOUT);
//     if (err != ESP_OK)
//     {
//         ESP_LOGE(TAG, "failed to read GPIO");
//         return err;
//     }

//     uint8_t response[11];
//     len = sizeof(response);
//     memcpy(response, (pn532->buffer + ACK_OFFSET), len);

//     gpio_state[0] = response[7]; // P3
//     gpio_state[1] = response[8]; // P7
//     gpio_state[2] = response[9]; // I0

//     return ESP_OK;
// }
