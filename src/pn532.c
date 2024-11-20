#include "pn532.h"
#include "pn532_types.h"

#include <string.h>

#include "esp_log.h"

#define PN532_MAX_CARDS 1

static const char* TAG = "pn532";

static uint8_t pn532_ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static uint8_t pn532_firmwareversion[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5};

extern esp_err_t pn532_uart_init(pn532_t* pn532, const pn532_uart_config_t* config);

esp_err_t pn532_init(pn532_handle_t* pn532_handle, const pn532_config_t* config) {
    if(!pn532_handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) malloc(sizeof(pn532_t));
    if(!pn532) {
        return ESP_ERR_NO_MEM;
    }
    memset(pn532, 0, sizeof(pn532_t));

    esp_err_t err = ESP_OK;
    switch(config->protocol) {
        case PN532_UART_PROTOCOL:
            err = pn532_uart_init(pn532, &config->uart);
            break;
        case PN532_I2C_PROTOCOL:
            // todo
            break;
        case PN532_SPI_PROTOCOL:
            // todo
            break;
        default:
            ESP_LOGE(TAG, "unknown protocol");
            free(pn532);
            err = ESP_ERR_INVALID_ARG;
            break;
    }

    if(err != ESP_OK) {
        return err;
    }

    *pn532_handle = pn532;
    return ESP_OK;
}

esp_err_t pn532_free(pn532_handle_t pn532_handle) {
    if(!pn532_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    esp_err_t err = pn532->free(pn532);
    if(err != ESP_OK) {
        return err;
    }

    free(pn532);

    return ESP_OK;
}

esp_err_t pn532_start(pn532_handle_t pn532_handle) {
    if(!pn532_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    vTaskDelay(PN532_DELAY_DEFAULT);
    esp_err_t err = pn532_send_command_check_ack(pn532, (uint8_t[]) {PN532_WAKEUP}, 1);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start pn532");
        return err;
    }
    
    return ESP_OK;
} 

esp_err_t pn532_send_command_check_ack(pn532_handle_t pn532_handle, uint8_t* command, uint8_t command_len) {
    if(!pn532_handle || !command) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;
    
    esp_err_t err = pn532->write_command(pn532, command, command_len);
    if(err != ESP_OK) {
        return err;
    }

    uint8_t response[sizeof(pn532_ack)];
    size_t len = sizeof(response);
    #ifdef PN532_DEBUG
        ESP_LOGD(TAG, "reading ack:");
    #endif
    err = pn532->read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(memcmp(response, pn532_ack, sizeof(pn532_ack)) != 0) {
        ESP_LOGE(TAG, "failed to check ack");
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t pn532_get_firmware_version(pn532_handle_t pn532_handle, uint8_t* version) {
    if(!pn532_handle || !version) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    esp_err_t err = pn532_send_command_check_ack(pn532, (uint8_t[]) {PN532_COMMAND_GETFIRMWAREVERSION}, 1);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to get firmware version");
        return err;
    }

    uint8_t response[10];
    size_t len = sizeof(response);
    err = pn532->read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(memcmp(response, pn532_firmwareversion, sizeof(pn532_firmwareversion)) != 0) {
        ESP_LOGE(TAG, "failed to check firmware version");
        return ESP_ERR_INVALID_RESPONSE;
    }

    version[0] = response[7]; // IC
    version[1] = response[8]; // IC version
    version[2] = response[9]; // firmware version
    version[3] = response[10]; // version of support protocol

    return ESP_OK;
}

esp_err_t pn532_SAM_configuration(pn532_handle_t pn532_handle) {
    if(!pn532_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_SAMCONFIGURATION,
        0x01, // normal mode
        0x14, // timeout 50ms * 20 = 1s
        0x01, // use IRQ pin
    };
    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure SAM");
        return err;
    }

    uint8_t response[8];
    size_t len = sizeof(response);
    err = pn532->read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(response[6] != 0x15) {
        ESP_LOGE(TAG, "failed to check SAM configuration");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t pn532_set_passive_activation_retries(pn532_handle_t pn532_handle, uint8_t max_retries) {
    if(!pn532_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

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

    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set passive activation retries");
        return err;
    }

    return ESP_OK;
}   

esp_err_t pn532_read_passive_target_id(pn532_handle_t pn532_handle, uint8_t card_baud_rate, uint8_t* uid, size_t* uid_len) {
    if(!pn532_handle || !uid || !uid_len) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    uint8_t command[] = {
        PN532_COMMAND_INLISTPASSIVETARGET,
        PN532_MAX_CARDS,
        card_baud_rate,
    };
    esp_err_t err = pn532_send_command_check_ack(pn532, command, sizeof(command));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to read passive target id");
        return err;
    }

    uint8_t response[20];
    size_t len = sizeof(response);
    err = pn532->read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(!response[7]) {
        ESP_LOGW(TAG, "no card detected");
        return ESP_ERR_NOT_FOUND;
    }

    *uid_len = response[12];
    for(size_t i = 0; i < *uid_len; i++) {
        uid[i] = response[13 + i];
    }

    return ESP_OK;
}