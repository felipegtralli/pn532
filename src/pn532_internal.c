#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "pn532_internal.h"

#define PN532_FRAME_COMMAND_OFFSET 6U

size_t pn532_command_framer(const uint8_t *command, size_t command_len, uint8_t *frame)
{
    assert((command != NULL) &&
           (frame != NULL));

    const uint8_t data_len = command_len + 1U;
    uint8_t checksum = PN532_HOSTTOPN532;

    frame[0] = PN532_PREAMBLE;
    frame[1] = PN532_STARTCODE1;
    frame[2] = PN532_STARTCODE2;
    frame[3] = data_len;
    frame[4] = ~data_len + 1U;
    frame[5] = PN532_HOSTTOPN532;

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
