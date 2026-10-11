#pragma once

#include <stddef.h>
#include <stdint.h>

#define PN532_FRAME_MAX_SIZE 262u
#define PN532_FRAME_MAX_PAYLOAD 252u
#define PN532_FRAME_HEADER_BYTES 7u
#define PN532_FRAME_TAIL_BYTES 2u
#define PN532_FRAME_OVERHEAD_BYTES                                             \
    (PN532_FRAME_HEADER_BYTES + PN532_FRAME_TAIL_BYTES)
#define PN532_FRAME_TFI_COMMAND_BYTES 2u /* TFI and command. */
#define PN532_FRAME_START_CODE 0xffu
#define PN532_FRAME_EXTENDED_LENGTH 0xffu
#define PN532_FRAME_HOST_TFI 0xd4u
#define PN532_FRAME_DEVICE_TFI 0xd5u
#define PN532_ACK_BYTES 6u

/* Normal PN532 information-frame fields. No extended frames are accepted. */
enum
{
    PN532_FRAME_LENGTH_BYTE = 3,
    PN532_FRAME_LENGTH_CHECK_BYTE,
    PN532_FRAME_TFI_BYTE,
    PN532_FRAME_COMMAND_BYTE,
};

/* Shared host ACK used for device acknowledgement and stale-command abort. */
extern const uint8_t pn532_ack[PN532_ACK_BYTES];

size_t pn532_frame_encode(uint8_t command,
                          const uint8_t *payload,
                          size_t payload_size,
                          uint8_t *frame,
                          size_t capacity);
/* Validate the complete normal-frame header before reading its payload. */
int pn532_frame_response_size(const uint8_t header[PN532_FRAME_HEADER_BYTES],
                              uint8_t command);
int pn532_frame_decode(const uint8_t *frame,
                       size_t frame_size,
                       uint8_t command,
                       uint8_t *response,
                       size_t capacity);
