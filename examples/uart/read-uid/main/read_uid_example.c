#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "pn532.h"

#define UART_TX_PIN CONFIG_UART_TX_GPIO_PIN
#define UART_RX_PIN CONFIG_UART_RX_GPIO_PIN
#define UART_PORT CONFIG_UART_PORT

#define UID_MAX_SIZE 10U
#define UID_READ_INTERVAL_MS 1000U
#define PASSIVE_ACTIVATION_RETRIES 1U

#define READ_UID_TASK_STACK_SIZE 4096U
#define READ_UID_TASK_PRIORITY 5U

static const char *TAG = "read-uid-example";

static void read_uid_task(void *parameters)
{
    PN532Handle pn532 = parameters;
    uint8_t uid[UID_MAX_SIZE] = {0};

    for (;;)
    {
        /* uid_len is input capacity and output length; reset it before every read. */
        size_t uid_len = sizeof(uid);

        const esp_err_t status = pn532_read_passive_target_id(pn532, PN532_MIFARE_ISO14443A, uid, &uid_len);
        if (status == ESP_OK)
        {
            ESP_LOGI(TAG, "UID read, length=%u", (unsigned int)uid_len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, uid, uid_len, ESP_LOG_INFO);
        }
        else if (status != ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to read UID: %s", esp_err_to_name(status));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(UID_READ_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "UID task stopped");

    const esp_err_t free_status = pn532_free(pn532);
    if (free_status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to free PN532: %s", esp_err_to_name(free_status));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    const PN532Config pn532_config = {
        .protocol = PN532_UART_PROTOCOL,
        .uart = {
            .tx = UART_TX_PIN,
            .rx = UART_RX_PIN,
            .uart_port = UART_PORT,
        },
    };
    PN532Handle pn532 = NULL;

    ESP_ERROR_CHECK(pn532_init(&pn532, &pn532_config));
    ESP_ERROR_CHECK(pn532_start(pn532));
    ESP_ERROR_CHECK(pn532_SAM_configuration(pn532));
    ESP_ERROR_CHECK(pn532_set_passive_activation_retries(pn532, PASSIVE_ACTIVATION_RETRIES));

    ESP_LOGI(TAG, "Present a Type A tag to read its UID");

    /* The task owns the handle after successful creation. */
    const BaseType_t task_status = xTaskCreate(read_uid_task, "read_uid_task", READ_UID_TASK_STACK_SIZE, pn532, READ_UID_TASK_PRIORITY, NULL);
    if (task_status != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UID task");
        ESP_ERROR_CHECK(pn532_free(pn532));
    }
}
