#pragma once

#include "cards/mifare/mifare_access.h"
#include "cards/mifare/tag/mifare_tag_session.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MIFARE_COMMAND_PHASE_IDLE = 0,
    MIFARE_COMMAND_PHASE_WRITE_DATA,
    MIFARE_COMMAND_PHASE_VALUE_DATA,
} mifare_command_phase_t;

typedef enum
{
    MIFARE_COMMAND_NO_RESPONSE = 0,
    MIFARE_COMMAND_RESPONSE,
    MIFARE_COMMAND_HALT,
} mifare_command_result_t;

/* A copy of the cipher at the next expected request length; live Crypto1
 * advances only when a frame is consumed. READ output also reuses a peeked
 * keystream. Preparation runs after TX and remains bounded to 22 bytes. */
typedef struct
{
    bool valid;
    uint8_t request_bytes;
    crypto1_state_t crypto_state_after_request;
    crypto1_state_t crypto_state_after_read;
    uint8_t request_keystream[MIFARE_BLOCK_FRAME_BYTES];
    uint8_t read_keystream[MIFARE_BLOCK_FRAME_BYTES];
    uint8_t read_filter[MIFARE_BLOCK_FRAME_BYTES];
} mifare_exchange_cache_t;

typedef struct
{
    mifare_command_phase_t phase;
    uint8_t pending_block;
    uint8_t pending_value_command;
    bool transfer_valid;
    uint8_t transfer_buffer[MIFARE_BLOCK_SIZE];
    uint8_t (*memory)[MIFARE_BLOCK_SIZE];
    const uint8_t *uid;
    mifare_access_t *access;
    mifare_tag_session_t *session;
    mifare_exchange_cache_t exchange_cache;
} mifare_commands_t;

void mifare_commands_init(mifare_commands_t *commands,
                          uint8_t (*memory)[MIFARE_BLOCK_SIZE],
                          const uint8_t uid[NFCA_UID_SINGLE_BYTES],
                          mifare_access_t *access,
                          mifare_tag_session_t *session);
void mifare_commands_reset(mifare_commands_t *commands);
void mifare_commands_prepare_next_request(mifare_commands_t *commands);

mifare_command_result_t
mifare_commands_handle(mifare_commands_t *commands,
                       const nfca_tag_request_t *request,
                       nfca_tag_response_t *response);
