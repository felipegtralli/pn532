#pragma once

#include <esp_err.h>

#include <driver/gpio.h>
#include <driver/uart.h>

#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00
#define PN532_HOSTTOPN532 0xD4

#define PN532_COMMAND_DIAGNOSE 0x00
#define PN532_COMMAND_GETFIRMWAREVERSION 0x02
#define PN532_COMMAND_GETGENERALSTATUS 0x04
#define PN532_COMMAND_READREGISTER 0x06
#define PN532_COMMAND_WRITEREGISTER 0x08
#define PN532_COMMAND_READGPIO 0x0C
#define PN532_COMMAND_WRITEGPIO 0x0E
#define PN532_COMMAND_SETSERIALBAUDRATE 0x10
#define PN532_COMMAND_SETPARAMETERS 0x12
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_POWERDOWN 0x16
#define PN532_COMMAND_RFCONFIGURATION 0x32
#define PN532_COMMAND_INJUMPFORDEP 0x56
#define PN532_COMMAND_INJUMPFORPSL 0x46
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INATR 0x50
#define PN532_COMMAND_INPSL 0x4E
#define PN532_COMMAND_INDATAEXCHANGE 0x40
#define PN532_COMMAND_INCOMMUNICATETHRU 0x42
#define PN532_COMMAND_INDESELECT 0x44
#define PN532_COMMAND_INRELEASE 0x52
#define PN532_COMMAND_INSELECT 0x54
#define PN532_COMMAND_INAUTOPOLL 0x60
#define PN532_COMMAND_TGINITASTARGET 0x8C
#define PN532_COMMAND_TGSETGENERALBYTES 0x92
#define PN532_COMMAND_TGGETDATA 0x86
#define PN532_COMMAND_TGSETDATA 0x8E
#define PN532_COMMAND_TGSETMETADATA 0x94
#define PN532_COMMAND_TGGETINITIATORCOMMAND 0x88
#define PN532_COMMAND_TGRESPONSETOINITIATOR 0x90
#define PN532_COMMAND_TGGETTARGETSTATUS 0x8A

#define PN532_WAKEUP 0x55

#define PN532_MIFARE_ISO14443A 0x00

typedef struct pn532_t* pn532_handle_t;

typedef struct {
    gpio_num_t tx;
    gpio_num_t rx;
    uart_port_t uart_port;
    uint32_t baud_rate;
} pn532_config_t;

esp_err_t pn532_init(pn532_handle_t* pn532_handle, const pn532_config_t* config);

esp_err_t pn532_free(pn532_handle_t pn532_handle);

esp_err_t pn532_start(pn532_handle_t pn532_handle);

esp_err_t pn532_send_command_check_ack(pn532_handle_t pn532_handle, uint8_t* command, uint8_t command_len);

esp_err_t pn532_get_firmware_version(pn532_handle_t pn532_handle, uint8_t* version);

esp_err_t pn532_SAM_configuration(pn532_handle_t pn532_handle);

esp_err_t pn532_read_passive_target_id(pn532_handle_t pn532_handle, uint8_t card_baud_rate, uint8_t* uid, size_t* uid_len);