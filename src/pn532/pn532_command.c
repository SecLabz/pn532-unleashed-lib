#include "pn532/pn532_command.h"

#include "pn532/pn532_frame.h"

#include "pico/stdlib.h"

#include <string.h>

static int failed(void)
{
    pn532_transport_end_read();
    /* Host ACK aborts any pending PN532 response before the next command. */
    (void)pn532_transport_write(pn532_ack, sizeof(pn532_ack));
    sleep_ms(PN532_COMMAND_ABORT_DELAY_MS);
    return -1;
}

int pn532_command(uint8_t command,
                  const uint8_t *payload,
                  size_t payload_size,
                  uint8_t *response,
                  size_t capacity)
{
    if (payload_size > PN532_COMMAND_MAX_PAYLOAD ||
        (payload_size != 0u && payload == NULL) ||
        (capacity != 0u && response == NULL))
    {
        return -1;
    }
    uint8_t packet[PN532_FRAME_MAX_SIZE];
    const size_t size = pn532_frame_encode(
        command, payload, payload_size, packet, sizeof(packet));
    if (size == 0u || !pn532_transport_write(packet, size) ||
        !pn532_transport_begin_read() ||
        !pn532_transport_read(packet, sizeof(pn532_ack)))
    {
        return failed();
    }
    pn532_transport_end_read();
    if (memcmp(packet, pn532_ack, sizeof(pn532_ack)) != 0 ||
        !pn532_transport_begin_read() ||
        !pn532_transport_read(packet, PN532_FRAME_HEADER_BYTES))
    {
        return failed();
    }
    const int response_size = pn532_frame_response_size(packet, command);
    if (response_size < 0 ||
        !pn532_transport_read(&packet[PN532_FRAME_HEADER_BYTES],
                              (size_t)response_size + PN532_FRAME_TAIL_BYTES))
    {
        return failed();
    }
    pn532_transport_end_read();
    const int decoded =
        pn532_frame_decode(packet,
                           (size_t)response_size + PN532_FRAME_OVERHEAD_BYTES,
                           command,
                           response,
                           capacity);
    return decoded < 0 ? failed() : decoded;
}

bool pn532_read_register(uint16_t address, uint8_t *value)
{
    const uint8_t payload[] = {(uint8_t)(address >> 8u), (uint8_t)address};
    return value != NULL &&
           pn532_command(
               PN532_CMD_READ_REGISTER, payload, sizeof(payload), value, 1u) ==
               1;
}

bool pn532_write_register(uint16_t address, uint8_t value)
{
    const uint8_t payload[] = {
        (uint8_t)(address >> 8u), (uint8_t)address, value};
    return pn532_command(
               PN532_CMD_WRITE_REGISTER, payload, sizeof(payload), NULL, 0u) ==
           0;
}

bool pn532_update_register(uint16_t address, uint8_t mask, uint8_t value)
{
    uint8_t before;
    uint8_t after;
    if (!pn532_read_register(address, &before))
    {
        return false;
    }
    const uint8_t next = (uint8_t)((before & ~mask) | (value & mask));
    return pn532_write_register(address, next) &&
           pn532_read_register(address, &after) &&
           (after & mask) == (next & mask);
}
