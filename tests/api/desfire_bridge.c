/* Inject independently constructed physical blocks into the ISO-DEP reader. */
#include "api_internal.h"
#include "pn532_unleashed/nfca_desfire.h"
#include "protocols/nfca/reader.h"

#include <assert.h>
#include <string.h>

typedef struct
{
    uint8_t command[32];
    uint8_t response[34];
    uint8_t command_size;
    uint8_t response_size;
    uint8_t fault;
    uint32_t elapsed_us;
} block_t;
static block_t blocks[128];
static uint16_t queued, consumed;
static uint32_t clock_us, last_timeout_us;
static nfc_state_t state;
static desfire_session_t session;

void host_desfire_reset(void)
{
    queued = consumed = 0u;
    clock_us = last_timeout_us = 0u;
    state = NFC_STATE_READING_NFCA;
    memset(&session, 0, sizeof(session));
}
void host_desfire_block(const uint8_t *command,
                        uint8_t command_size,
                        const uint8_t *response,
                        uint8_t response_size,
                        uint8_t fault,
                        uint32_t elapsed_us)
{
    assert(queued < 128u && command_size <= 32u && response_size <= 32u);
    block_t *block = &blocks[queued++];
    memcpy(block->command, command, command_size);
    memcpy(block->response, response, response_size);
    nfca_append_crc(block->response, response_size);
    block->command_size = command_size;
    block->response_size = response_size + 2u;
    block->fault = fault;
    block->elapsed_us = elapsed_us;
}
uint16_t host_desfire_consumed(void)
{
    return consumed;
}
uint32_t host_desfire_timeout(void)
{
    return last_timeout_us;
}
uint32_t host_desfire_clock(void)
{
    return clock_us;
}
int host_desfire_state(void)
{
    return state;
}
int host_desfire_activate(void)
{
    return desfire_activate(&session);
}
int host_desfire_exchange(const uint8_t *command,
                          uint8_t command_size,
                          uint8_t *reply,
                          uint16_t capacity,
                          size_t *size,
                          uint8_t *native_status)
{
    return desfire_exchange(
        &session, command, command_size, reply, capacity, size, native_status);
}
uint32_t time_us_32(void)
{
    return clock_us;
}
void sleep_us(uint64_t duration)
{
    clock_us += (uint32_t)duration;
}
nfc_status_t nfc_session_require(nfc_state_t expected)
{
    return state == expected ? NFC_OK : NFC_BUSY;
}
void nfc_session_error(void)
{
    state = NFC_STATE_ERROR;
}
nfc_status_t nfca_reader_select(nfca_tag_t *tag)
{
    *tag = (nfca_tag_t){.uid = {4, 1, 2, 3, 4, 5, 6},
                        .uid_size_bytes = 7u,
                        .atqa = {0x44, 3},
                        .sak = 0x20};
    return NFC_OK;
}
nfc_status_t nfca_reader_transfer_frame(const reader_frame_t *tx,
                                        nfca_pcd_response_t *rx,
                                        uint32_t timeout_us,
                                        bool expect_response)
{
    assert(expect_response && tx->byte_parity && consumed < queued);
    const block_t *block = &blocks[consumed++];
    assert(tx->length == block->command_size + 2u);
    assert(memcmp(tx->data, block->command, block->command_size) == 0);
    assert(nfca_check_crc(tx->data, tx->length));
    last_timeout_us = timeout_us;
    clock_us += block->elapsed_us;
    memset(rx, 0, sizeof(*rx));
    if (block->fault == 1u)
    {
        return NFC_NO_RESPONSE;
    }
    memcpy(rx->data, block->response, block->response_size);
    rx->size_bits = block->response_size * 8u;
    rx->parity_valid = block->fault != 2u;
    rx->framing_valid = true;
    if (block->fault == 3u)
    {
        rx->data[block->response_size - 1u] ^= 1u;
    }
    if (block->fault == 4u)
    {
        rx->size_bits--;
    }
    return NFC_OK;
}
