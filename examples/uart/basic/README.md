# UART basic example

Initializes the PN532, wakes it, selects SAM Normal mode, and prints its firmware
information. After a five-second countdown, it frees the handle and returns.

## Setup

Configure the module for UART (HSU) and power it according to its documentation.
Connect the signals using the default example pins:

| ESP32 | PN532 |
| --- | --- |
| GPIO17 (TX) | RX |
| GPIO16 (RX) | TX |
| GND | GND |

The default port is UART0; the driver uses a fixed baud rate of 115200. Change the
pins and port under **Example Configuration** in `menuconfig` to match your board.
Avoid sharing the port with console output.

## Run

From this directory, with the ESP-IDF environment loaded, select your chip target
(for example, `idf.py set-target esp32s3`), then run:

```sh
idf.py menuconfig
idf.py build flash monitor
```

Typical firmware output:

```text
Firmware IC=0x32, version=1.6, support=0x07
```
