#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

// Forward declaration of the PN532 SPI Object
typedef struct pn532_spi_s pn532_spi_t;

struct pn532_spi_s {
    // --- Member Variables (Instance Data) ---
    int cs_pin;
    int miso_pin;
    int mosi_pin;
    int sck_pin;
    int speed;
    spi_host_device_t host;
    spi_device_handle_t spi_handle;

    // --- Methods (Function Pointers) ---
    esp_err_t (*begin)(pn532_spi_t *self);
    esp_err_t (*reader_status)(pn532_spi_t *self);
    unsigned long (*get_pn532_data)(pn532_spi_t *self);
    void (*wakeup)(pn532_spi_t *self);
    esp_err_t (*send_command)(pn532_spi_t *self, const uint8_t *cmd_frame, size_t cmd_len);
    esp_err_t (*send_command_packet)(pn532_spi_t *self, uint8_t cmd_code, const uint8_t *cmd_params, uint8_t param_len);
    esp_err_t (*read_response)(pn532_spi_t *self, uint8_t *payload, uint8_t *payload_len);
};

// --- Constructor & Destructor Prototypes ---

/**
 * @brief Constructor: Allocates and initializes a new PN532 SPI object instance.
 * 
 * @param cs Chip Select GPIO pin
 * @param miso Master In Slave Out GPIO pin
 * @param mosi Master Out Slave In GPIO pin
 * @param sck Serial Clock GPIO pin
 * @param speed Clock speed in Hz (e.g. 2000000 for 2MHz)
 * @return Pointer to initialized pn532_spi_t instance, or NULL on allocation failure.
 */
pn532_spi_t* pn532_spi_create(int cs, int miso, int mosi, int sck, int speed);

/**
 * @brief Destructor: Releases SPI bus resources and frees object memory.
 * 
 * @param self Pointer to the pn532_spi_t instance
 */
void pn532_spi_destroy(pn532_spi_t *self);
