#include "pn532.h"
#include "pn532_types.h"

#include <esp_log.h>

#define PN532_UART_RX_BUF_SIZE 256
#define PN532_UART_TX_BUF_SIZE 256

static const char* TAG = "pn532";

static esp_err_t pn532_uart_write_command(pn532_t* pn532, uint8_t* command, uint8_t command_len) {
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

    #ifdef PN532_DEBUG
        ESP_LOGD(TAG, "writing command:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, cmd, sizeof(cmd), ESP_LOG_DEBUG);
    #endif

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

static esp_err_t pn532_uart_read_response(pn532_t* pn532, uint8_t* response, size_t response_len) {
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

    #ifdef PN532_DEBUG
        ESP_LOGD(TAG, "reading response:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, response, len, ESP_LOG_DEBUG);
    #endif

    xSemaphoreGive(pn532->mutex);
    return ESP_OK;
}

static esp_err_t pn532_uart_free(pn532_t* pn532) {
    esp_err_t err = uart_driver_delete(pn532->uart_port);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_delete failed: %d", err);
        return err;
    }

    vSemaphoreDelete(pn532->mutex);

    return ESP_OK;
}

esp_err_t pn532_uart_init(pn532_t* pn532, const pn532_uart_config_t* config) {
    pn532->protocol = PN532_UART_PROTOCOL;
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
        ESP_LOGE(TAG, "failed to create mutex");
        uart_driver_delete(pn532->uart_port);
        free(pn532);
        return ESP_ERR_NO_MEM;
    }

    pn532->write_command = pn532_uart_write_command;
    pn532->read_response = pn532_uart_read_response;
    pn532->free = pn532_uart_free;

    ESP_LOGI(TAG, "pn532 uart initialized");
    return ESP_OK;

ERR:
    free(pn532);
    return err;
}

