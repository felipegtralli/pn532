# PN532 for ESP-IDF

An ESP-IDF component for communicating with the PN532 NFC/RFID controller.

## Features

- UART (HSU) transport.
- Chip and firmware identification.
- SAM Normal mode configuration.
- Passive activation retry configuration.
- UID reading for one ISO/IEC 14443 Type A target, supporting 4-, 7-, and 10-byte UIDs.
- GPIO state reading.
- Generic command execution with ACK and response validation, and optional payload output.

I2C and SPI transports, extended frames, and automatic command retry or link
recovery are not implemented. Command IDs declared in the header do not imply
implemented wrappers or tested support for every chip feature.

## Tested setup

| Item | Version / model |
| --- | --- |
| ESP-IDF | 6.1.0 |
| ESP32 target | ESP32-S3 |
| PN532 module | Elechouse PN532 V3 |
| PN532 firmware | 1.6 |

These are the versions and hardware used for testing, not minimum requirements
or a guarantee of compatibility with other setups.

## Installation

Choose one method, then run `idf.py reconfigure` from your ESP-IDF project root.

### Git dependency

Add the dependency to your application's `main/idf_component.yml`:

```yaml
dependencies:
  pn532:
    git: https://github.com/felipegtralli/pn532.git
```

### Manual installation

Clone the component into your application's `components/` directory:

```sh
git clone https://github.com/felipegtralli/pn532.git components/pn532
```

If the component using this library lists its dependencies explicitly in
`CMakeLists.txt`, add `pn532` to its `REQUIRES` or `PRIV_REQUIRES` list.

## Basic usage

Select the module's matching host interface before use. This snippet uses UART;
adjust the pins to your wiring and choose a port not used by the console or
another component. UART uses a fixed baud rate of 115200.

```c
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#include "pn532.h"

void app_main(void)
{
    const PN532Config config = {
        .protocol = PN532_UART_PROTOCOL,
        .uart = {
            .tx = GPIO_NUM_17,
            .rx = GPIO_NUM_18,
            .uart_port = UART_NUM_1,
        },
    };
    PN532Handle pn532 = NULL;

    ESP_ERROR_CHECK(pn532_init(&pn532, &config));
    ESP_ERROR_CHECK(pn532_start(pn532));
    ESP_ERROR_CHECK(pn532_SAM_configuration(pn532));

    uint8_t version[4] = {0};
    ESP_ERROR_CHECK(pn532_get_firmware_version(pn532, version, sizeof(version)));
    ESP_LOGI("pn532-example", "Firmware %u.%u", (unsigned int)version[1], (unsigned int)version[2]);

    ESP_ERROR_CHECK(pn532_free(pn532));
}
```

`pn532_start()` performs the interface wake-up operation; it does not configure
the PN532 or verify communication. SAM configuration and command operations are
separate calls.

Call the API from task context, not an ISR. Stop all users of a handle before
freeing it. See [pn532.h](include/pn532.h) for buffer, timeout, and error semantics.
The snippet uses `ESP_ERROR_CHECK` to abort on errors; adapt error handling to
your application.

## Examples

- [Basic](examples/uart/basic/README.md): initialization and firmware information.
- [Read UID](examples/uart/read-uid/README.md): tag polling in a dedicated task.

Each example README includes wiring, configuration, and build instructions.

## Documentation

- [Development](docs/development.md)
- [Contributing](docs/contributing.md)
- [PN532 User Manual](docs/references/141520.pdf)
- [PN532/C1 Datasheet](docs/references/PN532_C1.pdf)

## License

Project code is licensed under the [MIT License](LICENSE). Manufacturer documents
in `docs/references/` retain their respective copyright notices.
