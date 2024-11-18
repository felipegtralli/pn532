#include "pn532.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#if CONFIG_LOG_DEFAULT_LEVEL >= 4 // 4 = LOG_LEVEL_DEBUG
#define PN532_DEBUG 1
#else
#define PN532_DEBUG 0
#endif

#define PN532_UART_RX_BUF_SIZE 256
#define PN532_UART_TX_BUF_SIZE 256

#define PN532_MAX_CARDS 1

#define PN532_DELAY_DEFAULT (500 / portTICK_PERIOD_MS)
#define PN532_DELAY(ms) (ms / portTICK_PERIOD_MS)

static const char* TAG = "pn532";

static uint8_t pn532_ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static uint8_t pn532_firmwareversion[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5};

typedef struct pn532_t {
    uart_port_t uart_port;
    SemaphoreHandle_t mutex;
} pn532_t;

static esp_err_t write_command(pn532_t* pn532, uint8_t* command, uint8_t command_len) {
    (void) uart_flush(pn532->uart_port);

    size_t data_len = command_len + 1;
    uint8_t cmd[data_len + 6];

    cmd[0] = PN532_PREAMBLE;
    cmd[1] = PN532_STARTCODE1;
    cmd[2] = PN532_STARTCODE2;
    cmd[3] = data_len;
    cmd[4] = ~data_len + 1;
    cmd[5] = PN532_HOSTTOPN532;

    for(size_t i = 0; i < command_len; i++) {
        cmd[6 + i] = command[i];
    }

    uint8_t checksum = PN532_HOSTTOPN532;
    for(size_t i = 0; i < command_len; i++) {
        checksum += command[i];
    }
    checksum = ~checksum + 1;
    
    cmd[6 + command_len] = checksum;
    cmd[7 + command_len] = PN532_POSTAMBLE;

    if(PN532_DEBUG) {
        ESP_LOGD(TAG, "writing command:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, cmd, sizeof(cmd), ESP_LOG_DEBUG);
    }

    if(xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "failed to take mutex");
        return ESP_FAIL;
    }

    int written = uart_write_bytes(pn532->uart_port, (const char*) cmd, sizeof(cmd));
    if(written != sizeof(cmd)) {
        ESP_LOGE(TAG, "failed to write command");
        xSemaphoreGive(pn532->mutex);
        return ESP_FAIL;
    }

    xSemaphoreGive(pn532->mutex);
    return ESP_OK;
}

static esp_err_t read_response(pn532_t* pn532, uint8_t* response, size_t response_len) {
    if(xSemaphoreTake(pn532->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "failed to take mutex");
        return ESP_FAIL;
    }

    vTaskDelay(PN532_DELAY_DEFAULT);
    int len = uart_read_bytes(pn532->uart_port, response, response_len, (PN532_DELAY_DEFAULT));
    if(len < 0) {
        ESP_LOGE(TAG, "failed to read response");
        xSemaphoreGive(pn532->mutex);
        return ESP_FAIL;
    }

    if(PN532_DEBUG) {
        ESP_LOGD(TAG, "reading response:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, response, len, ESP_LOG_DEBUG);
    }

    xSemaphoreGive(pn532->mutex);
    return ESP_OK;
}

esp_err_t pn532_init(pn532_handle_t* pn532_handle, const pn532_config_t* config) {
    if(!pn532_handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) malloc(sizeof(pn532_t));
    if(!pn532) {
        return ESP_ERR_NO_MEM;
    }
    memset(pn532, 0, sizeof(pn532_t));

    pn532->uart_port = config->uart_port;

    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(pn532->uart_port, &uart_config);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %d", err);
        goto ERR;
    }

    err = uart_set_pin(pn532->uart_port, config->tx, config->rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %d", err);
        goto ERR;
    }

    err = uart_driver_install(pn532->uart_port, PN532_UART_RX_BUF_SIZE, PN532_UART_TX_BUF_SIZE, 0, NULL, 0);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %d", err);
        goto ERR;
    }
    (void) uart_flush(pn532->uart_port);

    pn532->mutex = xSemaphoreCreateMutex();
    if(!pn532->mutex) {
        free(pn532);
        return ESP_ERR_NO_MEM;
    }

    *pn532_handle = pn532;

    ESP_LOGI(TAG, "pn532 initialized");
    return ESP_OK;

ERR:
    free(pn532);
    return err;
}

esp_err_t pn532_free(pn532_handle_t pn532_handle) {
    if(!pn532_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_t* pn532 = (pn532_t*) pn532_handle;

    esp_err_t err = uart_driver_delete(pn532->uart_port);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_delete failed: %d", err);
        return err;
    }

    vSemaphoreDelete(pn532->mutex);
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
    
    esp_err_t err = write_command(pn532, command, command_len);
    if(err != ESP_OK) {
        return err;
    }

    uint8_t response[sizeof(pn532_ack)];
    size_t len = sizeof(response);
    if(PN532_DEBUG) {
        ESP_LOGD(TAG, "reading ack:");
    }
    err = read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(memcmp(response, pn532_ack, sizeof(pn532_ack)) != 0) {
        ESP_LOGE(TAG, "failed to check ack");
        return ESP_FAIL;
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
    err = read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(memcmp(response, pn532_firmwareversion, sizeof(pn532_firmwareversion)) != 0) {
        ESP_LOGE(TAG, "failed to check firmware version");
        return ESP_FAIL;
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
    err = read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(response[6] != 0x15) {
        ESP_LOGE(TAG, "failed to check SAM configuration");
        return ESP_FAIL;
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
    err = read_response(pn532, response, len);
    if(err != ESP_OK) {
        return err;
    }

    if(response[7] != 1) {
        ESP_LOGE(TAG, "failed to read passive target id");
        return ESP_FAIL;
    }

    *uid_len = response[12];
    for(size_t i = 0; i < *uid_len; i++) {
        uid[i] = response[13 + i];
    }

    return ESP_OK;
}