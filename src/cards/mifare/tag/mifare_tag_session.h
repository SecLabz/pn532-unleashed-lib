#pragma once

#include "cards/mifare/mifare_access.h"
#include "common/crypto1.h"
#include "protocols/nfca/tag.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MIFARE_STATE_IDLE = 0,
    MIFARE_STATE_AUTHING,
    MIFARE_STATE_AUTHENTICATED,
} mifare_state_t;

typedef struct
{
    mifare_state_t state;
    uint32_t nonce_state;
    uint32_t next_nonce;
    bool next_nonce_ready;
    uint32_t authentication_nonce;
    uint8_t authentication_block;
    bool authentication_nested;
    uint32_t authentication_reader_answer;
    uint32_t authentication_card_answer;
    uint8_t authentication_reader_answer_bytes[MIFARE_NONCE_BYTES];
    uint8_t authentication_card_answer_bytes[MIFARE_NONCE_BYTES];
    uint8_t authentication_card_answer_parity[MIFARE_NONCE_BYTES];
    /* Initial AUTH sends a plaintext nonce; initialize Crypto1 after TX. */
    bool crypto_setup_pending;
    uint8_t crypto_setup_key[MIFARE_KEY_BYTES];
    uint32_t crypto_setup_input;
    crypto1_state_t crypto;
} mifare_tag_session_t;

void mifare_tag_session_init(mifare_tag_session_t *session,
                             mifare_access_t *access);
void mifare_tag_session_reset(mifare_tag_session_t *session,
                              mifare_access_t *access);

void mifare_tag_session_encrypt(mifare_tag_session_t *session,
                                nfca_tag_response_t *response,
                                const uint8_t *plain,
                                const uint8_t *cipher_input,
                                uint16_t len);
void mifare_tag_session_decrypt(mifare_tag_session_t *session,
                                const uint8_t *encrypted,
                                uint8_t *plain,
                                uint16_t len);
void mifare_tag_session_reply_nibble(mifare_tag_session_t *session,
                                     nfca_tag_response_t *response,
                                     uint8_t value);

bool mifare_tag_session_begin(mifare_tag_session_t *session,
                              mifare_access_t *access,
                              uint8_t (*memory)[MIFARE_BLOCK_SIZE],
                              const uint8_t uid[NFCA_UID_SINGLE_BYTES],
                              uint8_t command,
                              uint8_t block,
                              bool nested,
                              nfca_tag_response_t *response);
/* Verify the reader answer and return a deferred response. On success the
 * session stays AUTHING until the TX backend prepares its final response byte.
 * The session and response must remain alive throughout that transmission. */
bool mifare_tag_session_finish(mifare_tag_session_t *session,
                               mifare_access_t *access,
                               const nfca_tag_request_t *request,
                               nfca_tag_response_t *response);
void mifare_tag_session_prepare_answers(mifare_tag_session_t *session);
