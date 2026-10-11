#include "pn532/pn532_frame.h"

#include <string.h>

const uint8_t pn532_ack[PN532_ACK_BYTES] = {0u, 0u, 0xffu, 0u, 0xffu, 0u};

size_t pn532_frame_encode(uint8_t command,
                          const uint8_t *payload,
                          size_t payload_size,
                          uint8_t *frame,
                          size_t capacity)
{
    if (frame == NULL || payload_size > PN532_FRAME_MAX_PAYLOAD ||
        capacity < payload_size + PN532_FRAME_OVERHEAD_BYTES ||
        (payload_size != 0u && payload == NULL))
    {
        return 0u;
    }
    frame[0] = 0u;
    frame[1] = 0u;
    frame[2] = PN532_FRAME_START_CODE;
    frame[PN532_FRAME_LENGTH_BYTE] =
        (uint8_t)(payload_size + PN532_FRAME_TFI_COMMAND_BYTES);
    frame[PN532_FRAME_LENGTH_CHECK_BYTE] =
        (uint8_t)(0u - frame[PN532_FRAME_LENGTH_BYTE]);
    frame[PN532_FRAME_TFI_BYTE] = PN532_FRAME_HOST_TFI;
    frame[PN532_FRAME_COMMAND_BYTE] = command;
    uint8_t checksum = (uint8_t)(frame[PN532_FRAME_TFI_BYTE] + command);
    for (size_t index = 0u; index < payload_size; index++)
    {
        frame[PN532_FRAME_HEADER_BYTES + index] = payload[index];
        checksum = (uint8_t)(checksum + payload[index]);
    }
    frame[PN532_FRAME_HEADER_BYTES + payload_size] = (uint8_t)(0u - checksum);
    frame[PN532_FRAME_HEADER_BYTES + payload_size + 1u] = 0u;
    return payload_size + PN532_FRAME_OVERHEAD_BYTES;
}

int pn532_frame_response_size(const uint8_t header[PN532_FRAME_HEADER_BYTES],
                              uint8_t command)
{
    if (header == NULL || header[0] != 0u || header[1] != 0u ||
        header[2] != PN532_FRAME_START_CODE)
    {
        return -1;
    }
    const uint8_t length = header[PN532_FRAME_LENGTH_BYTE];
    if (length < PN532_FRAME_TFI_COMMAND_BYTES ||
        length == PN532_FRAME_EXTENDED_LENGTH ||
        (uint8_t)(length + header[PN532_FRAME_LENGTH_CHECK_BYTE]) != 0u)
    {
        return -1;
    }
    if (header[PN532_FRAME_TFI_BYTE] != PN532_FRAME_DEVICE_TFI ||
        header[PN532_FRAME_COMMAND_BYTE] != (uint8_t)(command + 1u))
    {
        return -1;
    }
    return length - PN532_FRAME_TFI_COMMAND_BYTES;
}

int pn532_frame_decode(const uint8_t *frame,
                       size_t frame_size,
                       uint8_t command,
                       uint8_t *response,
                       size_t capacity)
{
    if (frame == NULL || frame_size < PN532_FRAME_OVERHEAD_BYTES)
    {
        return -1;
    }
    const int size = pn532_frame_response_size(frame, command);
    if (size < 0 || (size_t)size + PN532_FRAME_OVERHEAD_BYTES != frame_size ||
        (size_t)size > capacity || (size != 0 && response == NULL) ||
        frame[frame_size - 1u] != 0u)
    {
        return -1;
    }
    uint8_t checksum = 0u;
    for (size_t index = PN532_FRAME_TFI_BYTE; index < frame_size - 1u; index++)
    {
        checksum = (uint8_t)(checksum + frame[index]);
    }
    if (checksum != 0u)
    {
        return -1;
    }
    if (size != 0)
    {
        memcpy(response, &frame[PN532_FRAME_HEADER_BYTES], (size_t)size);
    }
    return size;
}
