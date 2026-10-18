# UART UID reader example

Initializes and configures the PN532 in `app_main`, then starts `read_uid_task` to
poll for Type A tags. The task prints each detected UID and waits one second
between attempts. No tag is a normal result; other read errors stop the task and
trigger cleanup.

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

Present a Type A tag near the antenna. Example output (UID varies by tag):

```text
UID read, length=4
33 a3 3d 27
```
