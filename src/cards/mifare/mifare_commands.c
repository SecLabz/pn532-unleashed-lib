#include "cards/mifare/mifare_commands.h"

#include "common/compiler.h"
#include "common/endian.h"
#include "phy/nfca/picc/nfca_frame.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/nfca_profile.h"

#include <string.h>

void RAM_FUNC(mifare_commands_reset)(mifare_commands_t *commands)
{
    commands->phase = MIFARE_COMMAND_PHASE_IDLE;
    commands->pending_block = 0u;
    commands->pending_value_command = 0u;
    commands->transfer_valid = false;
    commands->exchange_cache.valid = false;
}

void mifare_commands_init(mifare_commands_t *commands,
                          uint8_t (*memory)[MIFARE_BLOCK_SIZE],
                          const uint8_t uid[NFCA_UID_SINGLE_BYTES],
                          mifare_access_t *access,
                          mifare_tag_session_t *session)
{
    commands->memory = memory;
    commands->uid = uid;
    commands->access = access;
    commands->session = session;
    mifare_commands_reset(commands);
}

static inline mifare_command_result_t reply_nibble(
    mifare_commands_t *commands, nfca_tag_response_t *response, uint8_t nibble)
{
    mifare_tag_session_reply_nibble(commands->session, response, nibble);
    return MIFARE_COMMAND_RESPONSE;
}

static bool
RAM_FUNC(mifare_value_block_valid)(const uint8_t block[MIFARE_BLOCK_SIZE])
{
    for (uint8_t index = 0u; index < MIFARE_VALUE_BYTES; index++)
    {
        const uint8_t inverted_value =
            (uint8_t)~block[index + MIFARE_VALUE_INVERTED_BYTE];
        if (block[index] != inverted_value ||
            block[index] != block[index + MIFARE_VALUE_COPY_BYTE])
        {
            return false;
        }
    }
    const uint8_t inverted_address =
        (uint8_t)~block[MIFARE_VALUE_ADDRESS_BYTE + 1u];
    const uint8_t inverted_address_copy =
        (uint8_t)~block[MIFARE_VALUE_ADDRESS_BYTE + 3u];
    return block[MIFARE_VALUE_ADDRESS_BYTE] == inverted_address &&
           block[MIFARE_VALUE_ADDRESS_BYTE] ==
               block[MIFARE_VALUE_ADDRESS_BYTE + 2u] &&
           block[MIFARE_VALUE_ADDRESS_BYTE + 2u] == inverted_address_copy;
}

static uint32_t RAM_FUNC(mifare_value_block_get)(const uint8_t *block)
{
    return read_le32(block);
}

static void RAM_FUNC(mifare_value_block_set)(uint8_t block[MIFARE_BLOCK_SIZE],
                                             uint32_t value)
{
    write_le32(block, value);
    for (uint8_t index = 0u; index < MIFARE_VALUE_BYTES; index++)
    {
        block[index + MIFARE_VALUE_INVERTED_BYTE] = (uint8_t)~block[index];
        block[index + MIFARE_VALUE_COPY_BYTE] = block[index];
    }
}

/* Shared plaintext construction stays inline in both SRAM response paths. */
static FORCE_INLINE void
build_read_plaintext(const mifare_commands_t *commands,
                     uint8_t block,
                     uint8_t plain[MIFARE_BLOCK_FRAME_BYTES])
{
    memcpy(plain, commands->memory[block], MIFARE_BLOCK_SIZE);

    if (block == commands->access->last_block)
    {
        const uint8_t permissions = commands->access->trailer_permissions;
        memset(plain, 0, MIFARE_KEY_BYTES);
        if ((permissions & MIFARE_TRAILER_READ_ACCESS) == 0u)
        {
            memset(&plain[MIFARE_TRAILER_ACCESS_BYTE],
                   0,
                   MIFARE_TRAILER_ACCESS_BYTES);
        }
        plain[MIFARE_TRAILER_USER_BYTE] =
            commands->memory[block][MIFARE_TRAILER_USER_BYTE];
        if ((permissions & MIFARE_TRAILER_READ_KEY_B) == 0u)
        {
            memset(&plain[MIFARE_TRAILER_KEY_B_BYTE], 0, MIFARE_KEY_BYTES);
        }
    }

    const uint16_t crc = nfca_crc16(plain, MIFARE_BLOCK_SIZE);
    write_le16(&plain[MIFARE_BLOCK_SIZE], crc);
}

static void RAM_FUNC(build_read_response)(mifare_commands_t *commands,
                                          uint8_t block,
                                          nfca_tag_response_t *response)
{
    uint8_t plain[MIFARE_BLOCK_FRAME_BYTES];
    build_read_plaintext(commands, block, plain);
    mifare_tag_session_encrypt(
        commands->session, response, plain, NULL, sizeof(plain));
    nfca_tag_response_data_with_parity(response, sizeof(plain));
}

static void RAM_FUNC(fast_build_read_response)(mifare_commands_t *commands,
                                               uint8_t block,
                                               nfca_tag_response_t *response)
{
    uint8_t plain[MIFARE_BLOCK_FRAME_BYTES];
    build_read_plaintext(commands, block, plain);

    const uint8_t *keystream = commands->exchange_cache.read_keystream;
    const uint8_t *filter = commands->exchange_cache.read_filter;
    for (uint16_t i = 0u; i < MIFARE_BLOCK_FRAME_BYTES; i++)
    {
        response->frame.bytes[i] = plain[i] ^ keystream[i];
        response->frame.parity[i] = nfca_odd_parity_bit(plain[i]) ^ filter[i];
    }
    nfca_tag_response_data_with_parity(response, MIFARE_BLOCK_FRAME_BYTES);
}

void mifare_commands_prepare_next_request(mifare_commands_t *commands)
{
    if (commands->session == NULL ||
        commands->session->state != MIFARE_STATE_AUTHENTICATED)
    {
        commands->exchange_cache.valid = false;
        return;
    }

    /* Peek without advancing the live cipher. The phase fixes the next
     * frame size; malformed frames still take the ordinary bounded path.
     * Do this after TX, before RX rearm, not inside the response deadline. */
    uint8_t request_bytes;
    if (commands->phase == MIFARE_COMMAND_PHASE_WRITE_DATA)
    {
        request_bytes = MIFARE_BLOCK_FRAME_BYTES;
    }
    else
    {
        request_bytes = commands->phase == MIFARE_COMMAND_PHASE_VALUE_DATA
                            ? MIFARE_VALUE_FRAME_BYTES
                            : MIFARE_COMMAND_FRAME_BYTES;
    }
    crypto1_state_t cipher = commands->session->crypto;
    crypto1_bytes_plain(&cipher,
                        NULL,
                        commands->exchange_cache.request_keystream,
                        NULL,
                        request_bytes);
    commands->exchange_cache.request_bytes = request_bytes;
    commands->exchange_cache.crypto_state_after_request = cipher;
    if (commands->phase == MIFARE_COMMAND_PHASE_IDLE)
    {
        crypto1_bytes_plain(&cipher,
                            NULL,
                            commands->exchange_cache.read_keystream,
                            commands->exchange_cache.read_filter,
                            MIFARE_BLOCK_FRAME_BYTES);
        commands->exchange_cache.crypto_state_after_read = cipher;
    }
    commands->exchange_cache.valid = true;
}

static FORCE_INLINE bool decrypt_request(mifare_commands_t *commands,
                                         const nfca_tag_request_t *request,
                                         uint8_t *plain)
{
    const bool cached =
        commands->exchange_cache.valid &&
        request->size_bytes == commands->exchange_cache.request_bytes;
    if (cached)
    {
        for (uint16_t i = 0u; i < request->size_bytes; i++)
        {
            plain[i] = request->bytes[i] ^
                       commands->exchange_cache.request_keystream[i];
        }
        /* Consume precisely the received bits before a CRC rejection/NACK. */
        commands->session->crypto =
            commands->exchange_cache.crypto_state_after_request;
    }
    else
    {
        mifare_tag_session_decrypt(
            commands->session, request->bytes, plain, request->size_bytes);
    }
    commands->exchange_cache.valid = false;
    return cached;
}

static FORCE_INLINE mifare_command_result_t finish_with_nibble(
    mifare_commands_t *commands, nfca_tag_response_t *response, uint8_t value)
{
    commands->phase = MIFARE_COMMAND_PHASE_IDLE;
    return reply_nibble(commands, response, value);
}

static NOINLINE void RAM_FUNC(write_sector_trailer)(mifare_commands_t *commands,
                                                    uint8_t block,
                                                    const uint8_t *data)
{
    uint8_t *trailer = commands->memory[block];
    const uint8_t permissions =
        mifare_trailer_permissions(commands->access, block);

    if ((permissions & MIFARE_TRAILER_WRITE_KEY_A) != 0u)
    {
        memcpy(trailer, data, MIFARE_KEY_BYTES);
    }
    if ((permissions & MIFARE_TRAILER_WRITE_ACCESS) != 0u)
    {
        memcpy(&trailer[MIFARE_TRAILER_ACCESS_BYTE],
               &data[MIFARE_TRAILER_ACCESS_BYTE],
               MIFARE_TRAILER_ACCESS_BYTES + 1u);
    }
    if ((permissions & MIFARE_TRAILER_WRITE_KEY_B) != 0u)
    {
        memcpy(&trailer[MIFARE_TRAILER_KEY_B_BYTE],
               &data[MIFARE_TRAILER_KEY_B_BYTE],
               MIFARE_KEY_BYTES);
    }
    mifare_access_update_cache(commands->access);
}

static NOINLINE mifare_command_result_t
RAM_FUNC(handle_write_data)(mifare_commands_t *commands,
                            const nfca_tag_request_t *request,
                            nfca_tag_response_t *response)
{
    uint8_t plain[MIFARE_BLOCK_FRAME_BYTES];
    if (request->size_bytes != sizeof(plain))
    {
        mifare_tag_session_reset(commands->session, commands->access);
        mifare_commands_reset(commands);
        return MIFARE_COMMAND_NO_RESPONSE;
    }
    (void)decrypt_request(commands, request, plain);

    if (!nfca_check_crc(plain, sizeof(plain)))
    {
        return finish_with_nibble(commands, response, MIFARE_NACK_CRC);
    }

    if (mifare_is_sector_trailer(commands->pending_block))
    {
        write_sector_trailer(commands, commands->pending_block, plain);
    }
    else if (mifare_data_operation_allowed(
                 commands->access, commands->pending_block, MIFARE_DATA_WRITE))
    {
        memcpy(commands->memory[commands->pending_block],
               plain,
               MIFARE_BLOCK_SIZE);
    }
    else
    {
        return finish_with_nibble(
            commands, response, MIFARE_NACK_NOT_AUTHENTICATED);
    }

    return finish_with_nibble(commands, response, MIFARE_ACK);
}

static NOINLINE mifare_command_result_t
RAM_FUNC(handle_value_data)(mifare_commands_t *commands,
                            const nfca_tag_request_t *request,
                            nfca_tag_response_t *response)
{
    uint8_t plain[MIFARE_VALUE_FRAME_BYTES];
    if (request->size_bytes != sizeof(plain))
    {
        mifare_tag_session_reset(commands->session, commands->access);
        mifare_commands_reset(commands);
        return MIFARE_COMMAND_NO_RESPONSE;
    }
    (void)decrypt_request(commands, request, plain);

    if (!nfca_check_crc(plain, sizeof(plain)))
    {
        return finish_with_nibble(commands, response, MIFARE_NACK_CRC);
    }

    memcpy(commands->transfer_buffer,
           commands->memory[commands->pending_block],
           MIFARE_BLOCK_SIZE);
    if (!mifare_value_block_valid(commands->transfer_buffer))
    {
        return finish_with_nibble(commands, response, MIFARE_NACK_OTHER);
    }

    const uint32_t operand = mifare_value_block_get(plain);
    uint32_t value = mifare_value_block_get(commands->transfer_buffer);
    if (commands->pending_value_command == MIFARE_CMD_INCREMENT)
    {
        value += operand;
    }
    else if (commands->pending_value_command == MIFARE_CMD_DECREMENT)
    {
        value -= operand;
    }
    mifare_value_block_set(commands->transfer_buffer, value);
    commands->transfer_valid = true;
    commands->phase = MIFARE_COMMAND_PHASE_IDLE;
    nfca_reply_silent(response);
    return MIFARE_COMMAND_NO_RESPONSE;
}

static NOINLINE mifare_command_result_t
RAM_FUNC(handle_other_commands)(mifare_commands_t *commands,
                                uint8_t cmd,
                                uint8_t block,
                                nfca_tag_response_t *response)
{
    switch (cmd)
    {
    case MIFARE_CMD_AUTH_A:
    case MIFARE_CMD_AUTH_B:
        return mifare_tag_session_begin(commands->session,
                                        commands->access,
                                        commands->memory,
                                        commands->uid,
                                        cmd,
                                        block,
                                        true,
                                        response)
                   ? MIFARE_COMMAND_RESPONSE
                   : MIFARE_COMMAND_NO_RESPONSE;

    case MIFARE_CMD_WRITE:
        if (!mifare_authenticated_for_block(commands->access, block) ||
            (!mifare_is_sector_trailer(block) &&
             !mifare_data_operation_allowed(
                 commands->access, block, MIFARE_DATA_WRITE)))
        {
            return reply_nibble(
                commands, response, MIFARE_NACK_NOT_AUTHENTICATED);
        }
        commands->pending_block = block;
        commands->phase = MIFARE_COMMAND_PHASE_WRITE_DATA;
        return reply_nibble(commands, response, MIFARE_ACK);

    case MIFARE_CMD_INCREMENT:
    case MIFARE_CMD_DECREMENT:
    case MIFARE_CMD_RESTORE:
    {
        const uint8_t required_access = (cmd == MIFARE_CMD_INCREMENT)
                                            ? MIFARE_DATA_INCREMENT
                                            : MIFARE_DATA_DECREMENT;
        if (!mifare_data_operation_allowed(
                commands->access, block, required_access))
        {
            return reply_nibble(
                commands, response, MIFARE_NACK_NOT_AUTHENTICATED);
        }
        commands->pending_block = block;
        commands->pending_value_command = cmd;
        commands->phase = MIFARE_COMMAND_PHASE_VALUE_DATA;
        return reply_nibble(commands, response, MIFARE_ACK);
    }

    case MIFARE_CMD_TRANSFER:
        if (!commands->transfer_valid ||
            !mifare_data_operation_allowed(
                commands->access, block, MIFARE_DATA_DECREMENT))
        {
            return reply_nibble(
                commands, response, MIFARE_NACK_NOT_AUTHENTICATED);
        }
        memcpy(commands->memory[block],
               commands->transfer_buffer,
               MIFARE_BLOCK_SIZE);
        commands->transfer_valid = false;
        return reply_nibble(commands, response, MIFARE_ACK);

    case NFCA_CMD_HALT:
        if (block == 0x00u)
        {
            mifare_tag_session_reset(commands->session, commands->access);
            mifare_commands_reset(commands);
            nfca_reply_silent(response);
            return MIFARE_COMMAND_HALT;
        }
        break;

    default:
        break;
    }

    return reply_nibble(commands, response, MIFARE_NACK_INVALID);
}

mifare_command_result_t
RAM_FUNC(mifare_commands_handle)(mifare_commands_t *commands,
                                 const nfca_tag_request_t *request,
                                 nfca_tag_response_t *response)
{
    if (commands->phase != MIFARE_COMMAND_PHASE_IDLE)
    {
        if (commands->phase == MIFARE_COMMAND_PHASE_WRITE_DATA)
        {
            return handle_write_data(commands, request, response);
        }
        if (commands->phase == MIFARE_COMMAND_PHASE_VALUE_DATA)
        {
            return handle_value_data(commands, request, response);
        }
    }
    if (request->size_bytes == 0u || request->size_bytes > NFCA_FRAME_MAX_BYTES)
    {
        return MIFARE_COMMAND_NO_RESPONSE;
    }

    uint8_t plain[NFCA_FRAME_MAX_BYTES];
    const bool cached = decrypt_request(commands, request, plain);
    if (request->size_bytes != MIFARE_COMMAND_FRAME_BYTES ||
        !nfca_check_crc(plain, MIFARE_COMMAND_FRAME_BYTES))
    {
        return reply_nibble(commands, response, MIFARE_NACK_INVALID);
    }

    const uint8_t cmd = plain[0];
    const uint8_t block = plain[1];

    if (cmd == MIFARE_CMD_READ)
    {
        if (!mifare_can_read_block(commands->access, block))
        {
            return reply_nibble(
                commands, response, MIFARE_NACK_NOT_AUTHENTICATED);
        }
        if (cached)
        {
            fast_build_read_response(commands, block, response);
            commands->session->crypto =
                commands->exchange_cache.crypto_state_after_read;
            commands->exchange_cache.valid = false;
            return MIFARE_COMMAND_RESPONSE;
        }
        build_read_response(commands, block, response);
        return MIFARE_COMMAND_RESPONSE;
    }

    return handle_other_commands(commands, cmd, block, response);
}
