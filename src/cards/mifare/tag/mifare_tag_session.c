#include "cards/mifare/tag/mifare_tag_session.h"

#include "common/compiler.h"
#include "common/endian.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/nfca_profile.h"

#include <string.h>

#define MIFARE_NONCE_INITIAL 0x01020304u

void RAM_FUNC(mifare_tag_session_reset)(mifare_tag_session_t *session,
                                        mifare_access_t *access)
{
    session->state = MIFARE_STATE_IDLE;
    session->crypto_setup_pending = false;
    session->authentication_nonce = 0u;
    session->authentication_block = 0u;
    session->authentication_nested = false;
    session->authentication_reader_answer = 0u;
    session->authentication_card_answer = 0u;
    memset(session->authentication_reader_answer_bytes,
           0,
           sizeof(session->authentication_reader_answer_bytes));
    memset(session->authentication_card_answer_bytes,
           0,
           sizeof(session->authentication_card_answer_bytes));
    memset(session->authentication_card_answer_parity,
           0,
           sizeof(session->authentication_card_answer_parity));
    crypto1_clear(&session->crypto);
    mifare_access_reset(access);
}

void mifare_tag_session_init(mifare_tag_session_t *session,
                             mifare_access_t *access)
{
    session->nonce_state = MIFARE_NONCE_INITIAL;
    session->next_nonce =
        crypto1_prng_successor(session->nonce_state, MIFARE_NONCE_PRNG_STEPS);
    session->next_nonce_ready = true;
    mifare_tag_session_reset(session, access);
}

static FORCE_INLINE uint32_t prng_successor(uint32_t value, uint32_t clocks)
{
    value = __builtin_bswap32(value);
    while (clocks-- != 0u)
    {
        const uint32_t feedback =
            (value >> 16u) ^ (value >> 18u) ^ (value >> 19u) ^ (value >> 21u);
        value = (value >> 1u) | ((feedback & 1u) << 31u);
    }
    return __builtin_bswap32(value);
}

void RAM_FUNC(mifare_tag_session_encrypt)(mifare_tag_session_t *session,
                                          nfca_tag_response_t *response,
                                          const uint8_t *plain,
                                          const uint8_t *cipher_input,
                                          uint16_t len)
{
    uint8_t keystream[MIFARE_BLOCK_FRAME_BYTES];
    uint8_t filter[MIFARE_BLOCK_FRAME_BYTES];
    crypto1_bytes_plain(&session->crypto, cipher_input, keystream, filter, len);
    for (uint16_t index = 0u; index < len; index++)
    {
        response->frame.bytes[index] = plain[index] ^ keystream[index];
        response->frame.parity[index] =
            nfca_odd_parity_bit(plain[index]) ^ filter[index];
    }
}

void RAM_FUNC(mifare_tag_session_decrypt)(mifare_tag_session_t *session,
                                          const uint8_t *encrypted,
                                          uint8_t *plain,
                                          uint16_t len)
{
    uint8_t keystream[NFCA_FRAME_MAX_BYTES];
    crypto1_bytes_plain(&session->crypto, NULL, keystream, NULL, len);
    for (uint16_t index = 0u; index < len; index++)
    {
        plain[index] = encrypted[index] ^ keystream[index];
    }
}

void RAM_FUNC(mifare_tag_session_reply_nibble)(mifare_tag_session_t *session,
                                               nfca_tag_response_t *response,
                                               uint8_t value)
{
    uint8_t encrypted = 0u;
    for (uint8_t bit = 0u; bit < NFCA_SHORT_RESPONSE_BITS; bit++)
    {
        encrypted |= (uint8_t)((((value >> bit) & 1u) ^
                                crypto1_bit(&session->crypto, 0u, false))
                               << bit);
    }
    nfca_reply_short(response, encrypted);
}

bool RAM_FUNC(mifare_tag_session_begin)(
    mifare_tag_session_t *session,
    mifare_access_t *access,
    uint8_t (*memory)[MIFARE_BLOCK_SIZE],
    const uint8_t uid[NFCA_UID_SINGLE_BYTES],
    uint8_t command,
    uint8_t block,
    bool nested,
    nfca_tag_response_t *response)
{
    if (!mifare_block_valid(access, block))
    {
        return false;
    }

    mifare_access_authenticate(
        access, mifare_block_sector(block), (uint8_t)(command & 1u));
    const uint8_t *trailer =
        memory[mifare_sector_trailer(access->authenticated_sector)];
    const uint8_t *key = access->authenticated_key == MIFARE_KEY_A
                             ? trailer
                             : &trailer[MIFARE_TRAILER_KEY_B_BYTE];

    session->nonce_state =
        session->next_nonce_ready
            ? session->next_nonce
            : prng_successor(session->nonce_state, MIFARE_NONCE_PRNG_STEPS);
    session->next_nonce_ready = false;
    session->authentication_nonce = session->nonce_state;
    session->authentication_block = block;
    session->authentication_nested = nested;
    session->authentication_reader_answer = 0u;
    session->authentication_card_answer = 0u;
    session->crypto_setup_pending = false;

    uint8_t nonce_bytes[MIFARE_NONCE_BYTES];
    write_be32(nonce_bytes, session->authentication_nonce);
    const uint8_t cipher_input[MIFARE_NONCE_BYTES] = {
        uid[0] ^ nonce_bytes[0],
        uid[1] ^ nonce_bytes[1],
        uid[2] ^ nonce_bytes[2],
        uid[3] ^ nonce_bytes[3],
    };
    if (!nested)
    {
        /* The nonce is plaintext: key loading and UID/nonce feedback are
         * needed only for the next reader frame. prepare_next_exchange runs
         * after nonce TX, while RX is still disabled. */
        memcpy(
            session->crypto_setup_key, key, sizeof(session->crypto_setup_key));
        session->crypto_setup_input = read_be32(cipher_input);
        session->crypto_setup_pending = true;
        memcpy(response->frame.bytes, nonce_bytes, sizeof(nonce_bytes));
        nfca_tag_response_data(response, MIFARE_NONCE_BYTES);
    }
    else
    {
        crypto1_init(&session->crypto, crypto1_key_from_bytes(key));
        mifare_tag_session_encrypt(
            session, response, nonce_bytes, cipher_input, MIFARE_NONCE_BYTES);
        nfca_tag_response_data_with_parity(response, MIFARE_NONCE_BYTES);
    }

    session->state = MIFARE_STATE_AUTHING;
    return true;
}

void RAM_FUNC(mifare_tag_session_prepare_answers)(mifare_tag_session_t *session)
{
    if (!session->next_nonce_ready)
    {
        session->next_nonce =
            prng_successor(session->nonce_state, MIFARE_NONCE_PRNG_STEPS);
        session->next_nonce_ready = true;
    }
    if (session->state == MIFARE_STATE_AUTHING && session->crypto_setup_pending)
    {
        crypto1_init(&session->crypto,
                     crypto1_key_from_bytes(session->crypto_setup_key));
        (void)crypto1_word(
            &session->crypto, session->crypto_setup_input, false);
        session->crypto_setup_pending = false;
    }
    if (session->state == MIFARE_STATE_AUTHING &&
        session->authentication_reader_answer == 0u)
    {
        session->authentication_reader_answer = prng_successor(
            session->authentication_nonce, MIFARE_READER_ANSWER_PRNG_STEPS);
        session->authentication_card_answer = prng_successor(
            session->authentication_nonce, MIFARE_CARD_ANSWER_PRNG_STEPS);
        write_be32(session->authentication_reader_answer_bytes,
                   session->authentication_reader_answer);
        write_be32(session->authentication_card_answer_bytes,
                   session->authentication_card_answer);
        for (uint8_t i = 0u; i < MIFARE_NONCE_BYTES; i++)
        {
            session->authentication_card_answer_parity[i] = nfca_odd_parity_bit(
                session->authentication_card_answer_bytes[i]);
        }
    }
}

static void RAM_FUNC(prepare_authentication_bytes)(
    nfca_tag_response_t *response, uint16_t first, uint16_t count)
{
    mifare_tag_session_t *session = response->prepare_context;
    crypto1_encrypt_authentication_response(
        &session->crypto,
        &session->authentication_card_answer_bytes[first],
        &session->authentication_card_answer_parity[first],
        &response->frame.bytes[first],
        &response->frame.parity[first],
        (uint8_t)count);
    if (first + count == MIFARE_NONCE_BYTES)
    {
        session->state = MIFARE_STATE_AUTHENTICATED;
    }
}

bool RAM_FUNC(mifare_tag_session_finish)(mifare_tag_session_t *session,
                                         mifare_access_t *access,
                                         const nfca_tag_request_t *request,
                                         nfca_tag_response_t *response)
{
    if (request->size_bytes != MIFARE_READER_AUTH_BYTES)
    {
        mifare_tag_session_reset(session, access);
        return false;
    }

    if (session->crypto_setup_pending ||
        session->authentication_reader_answer == 0u)
    {
        mifare_tag_session_prepare_answers(session);
    }

    if (!crypto1_verify_authentication(
            &session->crypto,
            request->bytes,
            &request->bytes[MIFARE_NONCE_BYTES],
            session->authentication_reader_answer_bytes))
    {
        mifare_tag_session_reset(session, access);
        return false;
    }

    /* The first encrypted bit needs only the current filter, without
     * clocking Crypto1. Two queued symbols give the first byte producer
     * 256/fc (1024 CPU cycles at 54.24 MHz) before the FIFO can run dry. */
    response->frame.bytes[0] =
        (uint8_t)((session->authentication_card_answer_bytes[0] ^
                   crypto1_filter(&session->crypto)) &
                  1u);
    response->frame.type = NFCA_TAG_RESPONSE_DEFERRED_PARITY;
    response->frame.size_bytes = MIFARE_NONCE_BYTES;
    response->frame.short4 = 0u;
    response->prepare_bytes = prepare_authentication_bytes;
    response->prepare_context = session;
    return true;
}
