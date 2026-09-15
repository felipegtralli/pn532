#include "pn532_spi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- SPI Callbacks (Extracted dynamically from self) ---
static void IRAM_ATTR spi_pre_cb(spi_transaction_t *trans) {
    pn532_spi_t *self = (pn532_spi_t *)trans->user;
    if (self && self->cs_pin >= 0) {
        gpio_set_level(self->cs_pin, 0); // Pull CS LOW
        esp_rom_delay_us(100);          // Wait for PN532 wakeup setup time
    }
}

static void IRAM_ATTR spi_post_cb(spi_transaction_t *trans) {
    pn532_spi_t *self = (pn532_spi_t *)trans->user;
    if (self && self->cs_pin >= 0) {
        gpio_set_level(self->cs_pin, 1); // Pull CS HIGH
    }
}

// --- Method Implementations ---

static void pn532_spi_method_wakeup(pn532_spi_t *self) {
    if (!self) return;
    gpio_set_level(self->cs_pin, 0);
    esp_rom_delay_us(2000); // Pulse CS LOW for 2ms to wake up chip
    gpio_set_level(self->cs_pin, 1);
}

static esp_err_t pn532_spi_method_send_command(pn532_spi_t *self, const uint8_t *cmd_frame, size_t cmd_len) {
    if (!self || !self->spi_handle) return ESP_ERR_INVALID_ARG;

    vTaskDelay(1); // Ensure CS was idle HIGH for at least 10ms

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.user = (void *)self;
    t.length = 8 * cmd_len;
    t.tx_buffer = cmd_frame;

    esp_err_t err = spi_device_transmit(self->spi_handle, &t);
    if (err != ESP_OK) return err;

    // Poll status until ready for ACK
    uint8_t status_tx[2] = {0x02, 0x00};
    uint8_t status_rx[2] = {0};
    int timeout = 100;

    while (timeout--) {
        memset(&t, 0, sizeof(t));
        t.user = (void *)self;
        t.length = 8 * 2;
        t.tx_buffer = status_tx;
        t.rx_buffer = status_rx;

        err = spi_device_transmit(self->spi_handle, &t);
        if (err != ESP_OK) return err;
        if ((status_rx[1] & 0x01) == 0x01) {
            break; // Ready!
        }
        vTaskDelay(1);
    }
    if (timeout < 0) {
        printf("   [DEBUG] Status check timed out! Last status_rx: %02X %02X\n", status_rx[0], status_rx[1]);
        return ESP_ERR_TIMEOUT;
    }

    // Read ACK Frame (8 bytes)
    uint8_t read_tx[10] = {0};
    read_tx[0] = 0x03; // Data Read
    uint8_t ack_rx[10] = {0};

    memset(&t, 0, sizeof(t));
    t.user = (void *)self;
    t.length = 8 * 8;
    t.tx_buffer = read_tx;
    t.rx_buffer = ack_rx;

    err = spi_device_transmit(self->spi_handle, &t);
    if (err != ESP_OK) return err;

    // Verify ACK Frame sequence (00 00 FF 00 FF 00)
    int ack_idx = -1;
    for (int i = 1; i <= 3; i++) {
        if (ack_rx[i] == 0x00 && ack_rx[i+1] == 0x00 && ack_rx[i+2] == 0xFF &&
            ack_rx[i+3] == 0x00 && ack_rx[i+4] == 0xFF && ack_rx[i+5] == 0x00) {
            ack_idx = i;
            break;
        }
    }
    if (ack_idx == -1) {
        printf("   [DEBUG] Invalid ACK Frame received: ");
        for (int i = 0; i < 8; i++) {
            printf("%02X ", ack_rx[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t pn532_spi_method_send_command_packet(pn532_spi_t *self, uint8_t cmd_code, const uint8_t *cmd_params, uint8_t param_len) {
    if (!self) return ESP_ERR_INVALID_ARG;

    uint8_t tx_frame[64] = {0};
    int idx = 0;

    tx_frame[idx++] = 0x01; // SPI Data Write Indicator
    tx_frame[idx++] = 0x00; // Preamble
    tx_frame[idx++] = 0x00; // Start Code 1
    tx_frame[idx++] = 0xFF; // Start Code 2

    uint8_t len = param_len + 2; // TFI (0xD4) + Command Code + Params
    tx_frame[idx++] = len;
    tx_frame[idx++] = (uint8_t)(0x100 - len); // Length checksum

    tx_frame[idx++] = 0xD4; // TFI
    tx_frame[idx++] = cmd_code;

    uint16_t sum = 0xD4 + cmd_code;
    for (int i = 0; i < param_len; i++) {
        tx_frame[idx++] = cmd_params[i];
        sum += cmd_params[i];
    }

    tx_frame[idx++] = (uint8_t)(~sum + 1); // Data checksum
    tx_frame[idx++] = 0x00; // Postamble

    return self->send_command(self, tx_frame, idx);
}

static esp_err_t pn532_spi_method_read_response(pn532_spi_t *self, uint8_t *payload, uint8_t *payload_len) {
    if (!self || !self->spi_handle || !payload || !payload_len) return ESP_ERR_INVALID_ARG;

    uint8_t status_tx[2] = {0x02, 0x00};
    uint8_t status_rx[2] = {0};
    int timeout = 200;
    spi_transaction_t t;
    esp_err_t err;

    while (timeout--) {
        memset(&t, 0, sizeof(t));
        t.user = (void *)self;
        t.length = 8 * 2;
        t.tx_buffer = status_tx;
        t.rx_buffer = status_rx;

        err = spi_device_transmit(self->spi_handle, &t);
        if (err != ESP_OK) return err;
        if ((status_rx[1] & 0x01) == 0x01) {
            break; // Ready!
        }
        vTaskDelay(1);
    }
    if (timeout < 0) {
        return ESP_ERR_TIMEOUT;
    }

    // Read Response Frame (32 bytes)
    uint8_t rx_raw[32] = {0};
    uint8_t tx_read[32] = {0};
    tx_read[0] = 0x03; // Data Read

    memset(&t, 0, sizeof(t));
    t.user = (void *)self;
    t.length = 8 * 32;
    t.tx_buffer = tx_read;
    t.rx_buffer = rx_raw;

    err = spi_device_transmit(self->spi_handle, &t);
    if (err != ESP_OK) return err;

    // Scan for header 00 00 FF
    int header_idx = -1;
    for (int i = 1; i < 28; i++) {
        if (rx_raw[i] == 0x00 && rx_raw[i+1] == 0x00 && rx_raw[i+2] == 0xFF) {
            header_idx = i;
            break;
        }
    }
    if (header_idx == -1) {
        return ESP_FAIL;
    }

    // Validate length checksum
    uint8_t len = rx_raw[header_idx + 3];
    uint8_t len_chk = rx_raw[header_idx + 4];
    if ((uint8_t)(len + len_chk) != 0) {
        return ESP_FAIL;
    }

    // Validate TFI (0xD5)
    uint8_t tfi = rx_raw[header_idx + 5];
    if (tfi != 0xD5) {
        return ESP_FAIL;
    }

    uint8_t p_len = len - 1;
    if (p_len > *payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int i = 0; i < p_len; i++) {
        payload[i] = rx_raw[header_idx + 6 + i];
    }
    *payload_len = p_len;

    // Verify data checksum
    uint8_t sum = 0xD5;
    for (int i = 0; i < p_len; i++) {
        sum += payload[i];
    }
    uint8_t data_chk = rx_raw[header_idx + 6 + p_len];
    if ((uint8_t)(sum + data_chk) != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t pn532_spi_method_begin(pn532_spi_t *self) {
    if (!self) return ESP_ERR_INVALID_ARG;

    gpio_set_direction(self->cs_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(self->cs_pin, 1); // CS Idle HIGH

    spi_bus_config_t buscfg = {
        .mosi_io_num = self->mosi_pin,
        .miso_io_num = self->miso_pin,
        .sclk_io_num = self->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(self->host, &buscfg, SPI_DMA_CH_AUTO);
    gpio_pullup_en(self->miso_pin);

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = self->speed,
        .spics_io_num = -1, // Manual CS handling in callbacks
        .queue_size = 1,
        .flags = SPI_DEVICE_BIT_LSBFIRST,
        .pre_cb = spi_pre_cb,
        .post_cb = spi_post_cb,
    };

    esp_err_t err = spi_bus_add_device(self->host, &devcfg, &self->spi_handle);
    if (err != ESP_OK) return err;

    // Wake up PN532 chip
    self->wakeup(self);

    printf("\n--- Starting PN532 Diagnostics ---\n");

    // STEP 1: GetFirmwareVersion Command (0x02)
    uint8_t payload[32];
    uint8_t payload_len;
    uint8_t sam_params[] = {0x01, 0x14, 0x01};

    printf("1. Sending GetFirmwareVersion command...\n");
    if (self->send_command_packet(self, 0x02, NULL, 0) == ESP_OK) {
        payload_len = sizeof(payload);
        esp_err_t status = self->read_response(self, payload, &payload_len);
        if (status != ESP_OK) {
            printf("   Failed to read GetFirmwareVersion response!\n");
            return status;
        }
    } else {
        printf("   Failed to send GetFirmwareVersion command (ACK check failed)!\n");
        return ESP_FAIL;
    }

    // STEP 2: SAMConfiguration Command (0x14)
    printf("2. Sending SAMConfiguration command...\n");
    if (self->send_command_packet(self, 0x14, sam_params, 3) == ESP_OK) {
        payload_len = sizeof(payload);
        esp_err_t status = self->read_response(self, payload, &payload_len);
        if (status != ESP_OK) {
            printf("   Failed to read SAMConfiguration response!\n");
            return status;
        }
    } else {
        printf("   Failed to send SAMConfiguration (ACK check failed)!\n");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t pn532_spi_method_reader_status(pn532_spi_t *self) {
    if (!self) return ESP_ERR_INVALID_ARG;
    uint8_t scan_params[] = {0x01, 0x00}; // Max targets: 1, ISO14443A 106 kbps
    return self->send_command_packet(self, 0x4A, scan_params, 2);
}

static unsigned long pn532_spi_method_get_pn532_data(pn532_spi_t *self) {
    if (!self) return -2;

    uint8_t payload[32];
    uint8_t payload_len = sizeof(payload);
    esp_err_t err = self->read_response(self, payload, &payload_len);

    if (err == ESP_OK) {
        if (payload[0] == 0x4B && payload[1] > 0) {
            uint8_t uid_len = payload[6];
            unsigned long card_id = 0;
            for (int i = 0; i < uid_len; i++) {
                printf("%02X ", payload[7 + i]);
                card_id |= ((unsigned long)payload[7 + i] << (8 * i));
            }
            return card_id;
        }
        return -2;
    } else if (err == ESP_ERR_TIMEOUT) {
        return -1;
    } else {
        return -2;
    }
}

// --- Constructor ---
pn532_spi_t* pn532_spi_create(int cs, int miso, int mosi, int sck, int speed) {
    pn532_spi_t *self = (pn532_spi_t *)malloc(sizeof(pn532_spi_t));
    if (!self) return NULL;

    // Set member variables
    self->cs_pin = cs;
    self->miso_pin = miso;
    self->mosi_pin = mosi;
    self->sck_pin = sck;
    self->speed = speed;
    self->host = SPI2_HOST;
    self->spi_handle = NULL;

    // Bind method function pointers
    self->begin = pn532_spi_method_begin;
    self->reader_status = pn532_spi_method_reader_status;
    self->get_pn532_data = pn532_spi_method_get_pn532_data;
    self->wakeup = pn532_spi_method_wakeup;
    self->send_command = pn532_spi_method_send_command;
    self->send_command_packet = pn532_spi_method_send_command_packet;
    self->read_response = pn532_spi_method_read_response;

    return self;
}

// --- Destructor ---
void pn532_spi_destroy(pn532_spi_t *self) {
    if (!self) return;

    if (self->spi_handle) {
        spi_bus_remove_device(self->spi_handle);
        spi_bus_free(self->host);
    }
    free(self);
}

// Integration with core pn532_init()
esp_err_t pn532_spi_init(pn532_t* pn532, const pn532_spi_config_t* config) {
    if (!pn532 || !config) return ESP_ERR_INVALID_ARG;
    pn532->protocol = PN532_SPI_PROTOCOL;
    pn532->spi.ss = config->ss;
    pn532->spi.host = config->host;
    return ESP_OK;
}
