/**
 * @file pn532_internal.c
 * @brief Shared normal-frame encoding and response validation, without I/O.
 *
 * Frame layout: UM0701-02 Rev. 02, Section 6.2.1.1, p. 28.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "pn532.h"
#include "pn532_internal.h"

#define PN532_FRAME_DATA_OFFSET 5U
#define PN532_FRAME_COMMAND_OFFSET 6U

#define PN532_HOSTTOPN532 0xD4
#define PN532_ERROR_CODE 0x7FU
#define PN532_RESPONSE_TFI 0xD5U

size_t pn532_command_framer(const uint8_t *command, size_t command_len, uint8_t *frame)
{
    assert((command != NULL) &&
           (frame != NULL));

    /* LEN includes TFI; byte-sized checksum arithmetic keeps only the low eight bits. */
    const uint8_t data_len = command_len + 1U;
    uint8_t checksum = PN532_HOSTTOPN532;

    frame[PN532_FRAME_PREAMBLE_OFFSET] = PN532_PREAMBLE;
    frame[PN532_FRAME_STARTCODE1_OFFSET] = PN532_STARTCODE1;
    frame[PN532_FRAME_STARTCODE2_OFFSET] = PN532_STARTCODE2;
    frame[PN532_FRAME_LEN_OFFSET] = data_len;
    frame[PN532_FRAME_LCS_OFFSET] = ~data_len + 1U;
    frame[PN532_FRAME_DATA_OFFSET] = PN532_HOSTTOPN532;

    for (size_t i = 0U; i < command_len; i++)
    {
        frame[PN532_FRAME_COMMAND_OFFSET + i] = command[i];
        checksum += command[i];
    }

    const size_t checksum_offset = PN532_FRAME_COMMAND_OFFSET + command_len;
    const size_t postamble_offset = checksum_offset + 1U;

    frame[checksum_offset] = ~checksum + 1U;
    frame[postamble_offset] = PN532_POSTAMBLE;

    return PN532_FRAME_OVERHEAD + command_len;
}

esp_err_t pn532_response_parser(const uint8_t *frame, size_t frame_len, uint8_t expected_command, const uint8_t **payload, size_t *payload_len)
{
    assert((frame != NULL) &&
           (frame_len >= PN532_FRAME_HEADER_SIZE) &&
           (payload != NULL) &&
           (payload_len != NULL));

    *payload = NULL;
    *payload_len = 0U;

    const uint8_t data_len = frame[PN532_FRAME_LEN_OFFSET];
    const size_t expected_frame_len = PN532_FRAME_HEADER_SIZE + data_len + PN532_FRAME_TRAILER_SIZE;
    if (frame_len != expected_frame_len)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame[PN532_FRAME_DATA_OFFSET + data_len + 1U] != PN532_POSTAMBLE)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t checksum = 0U;
    for (size_t i = PN532_FRAME_DATA_OFFSET; i < (frame_len - 1U); i++)
    {
        checksum += frame[i];
    }

    if (checksum != 0U)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* UM0701-02 Rev. 02, Section 6.2.1.5, p. 31: error frames have no TFI or response ID. */
    if ((data_len == 1U) &&
        (frame[PN532_FRAME_DATA_OFFSET] == PN532_ERROR_CODE))
    {
        return ESP_FAIL;
    }

    if ((data_len < 2U) ||
        (frame[PN532_FRAME_DATA_OFFSET] != PN532_RESPONSE_TFI) ||
        (frame[PN532_FRAME_COMMAND_OFFSET] != expected_command))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *payload = frame + PN532_FRAME_COMMAND_OFFSET + 1U;
    *payload_len = data_len - 2U;

    return ESP_OK;
}
