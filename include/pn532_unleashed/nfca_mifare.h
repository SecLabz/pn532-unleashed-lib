#pragma once

#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/status.h"

/* Single foreground Classic session. Explicit geometry (SAK cannot distinguish
 * 1K/2K); nfca_select requires initialized IDLE. Field stays on between
 * operations. After a successful selection, call nfc_stop on completion or
 * operation failure. Failed selection releases any RF it acquired; rejection
 * preserves other roles. Stop clears every recovery context. No pointers are
 * retained. Output is valid only after NFC_OK unless stated. All
 * authentication/challenge timing executes locally, independent of USB.
 */
typedef struct
{
    uint8_t block;
    bool key_b;
    uint8_t key[MIFARE_KEY_BYTES];
} mifare_source_t;

typedef struct
{
    uint32_t source_nonce;
    uint32_t encrypted_nonce;
    uint8_t parity[MIFARE_NONCE_BYTES]; /* Raw wire parity, not plain parity. */
} mifare_observation_t;

typedef enum
{
    DARKSIDE_MORE = 0,
    DARKSIDE_COMPLETE,
    DARKSIDE_NO_NACK,
    DARKSIDE_HARD_PRNG,
    DARKSIDE_SYNC_FAILED,
    DARKSIDE_AUTH_RESPONSE,
    DARKSIDE_EXHAUSTED,
} darkside_outcome_t;

typedef struct
{
    darkside_outcome_t outcome;
    uint32_t uid;
    uint32_t nonce;
    uint32_t reader_nonce;
    uint32_t reader_answer;
    uint8_t parity[8];
    uint8_t keystream[8];
} darkside_observation_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* One key, fresh authentication. ACCESS_DENIED means no match and leaves
     * selection available for another candidate. Caller owns candidate order.
     */
    nfc_status_t nfca_mifare_check_key(uint8_t block,
                                       bool key_b,
                                       const uint8_t key[MIFARE_KEY_BYTES]);
    /* Reads include manufacturer blocks and masked trailers. writable means
     * a permitted user-data write. Outputs change only on success. */
    nfc_status_t nfca_mifare_read_block(uint8_t block,
                                        bool key_b,
                                        const uint8_t key[MIFARE_KEY_BYTES],
                                        uint8_t data[16],
                                        bool *writable);
    nfc_status_t nfca_mifare_write_block(uint8_t block,
                                         bool key_b,
                                         const uint8_t key[MIFARE_KEY_BYTES],
                                         const uint8_t before[16],
                                         const uint8_t after[16]);
    /* Capacity covers 4 or 16 blocks. Size is zero on failure; discard partial
     * output. Masked secrets remain observations. */
    nfc_status_t nfca_mifare_read_sector(uint8_t sector,
                                         bool key_b,
                                         const uint8_t key[MIFARE_KEY_BYTES],
                                         uint8_t *memory,
                                         uint16_t capacity_bytes,
                                         uint16_t *size_bytes);
    nfc_status_t nfca_mifare_probe(uint32_t *nonce);
    /* One fresh source authentication and target encrypted nonce.
     * Scheduled=true uses the same fixed deadlines for calibration and
     * unknown-key collection. false uses the Hardnested acquisition cadence. */
    nfc_status_t nfca_mifare_collect(const mifare_source_t *source,
                                     uint8_t target_block,
                                     bool target_key_b,
                                     bool scheduled,
                                     mifare_observation_t *observation);
    /* Static Nested: optionally complete one additional known-source AUTH
     * before requesting the target nonce. source_nonce is the last known
     * source challenge, decrypted and authenticated. No keys are retained;
     * output changes only on NFC_OK. Work is bounded to two complete source
     * authentications and one incomplete target challenge, then expiration. */
    nfc_status_t nfca_mifare_collect_static(const mifare_source_t *source,
                                            uint8_t target_block,
                                            bool target_key_b,
                                            bool advance_source,
                                            mifare_observation_t *observation);
    /* Explicit trailer management in a validated nfca_select() session.
     * All arrays are borrowed for the call. before is the exact 16-byte
     * observable trailer (masked secrets included); after contains both new
     * keys, valid access bytes and GPB. Check is read-only. Both functions
     * release the owned selection on return. WRITE is sent once, only after
     * fresh credential/permission/content checks; new A and usable B keys
     * and readable metadata are verified. Unknown post-write outcomes return
     * NFC_WRITE_OUTCOME_UNKNOWN and must never be retried automatically.
     * Generic user-data writes still reject trailers. */
    nfc_status_t nfca_mifare_trailer_check(uint8_t sector,
                                           bool key_b,
                                           const uint8_t key[MIFARE_KEY_BYTES],
                                           const uint8_t before[16],
                                           const uint8_t after[16]);
    nfc_status_t nfca_mifare_trailer_write(uint8_t sector,
                                           bool key_b,
                                           const uint8_t key[MIFARE_KEY_BYTES],
                                           const uint8_t before[16],
                                           const uint8_t after[16]);
    /* Darkside is explicit, bounded work: start clears its context, step
     * performs one synchronization attempt. NFC_OK with MORE requests another
     * step. Each step completes the challenge and safely reselects before
     * returning to USB. variant changes the reader nonce for another
     * independent observation set.
     */
    nfc_status_t
    nfca_mifare_darkside_start(uint8_t block, bool key_b, uint8_t variant);
    nfc_status_t nfca_mifare_darkside_step(darkside_observation_t *result);

#ifdef __cplusplus
}
#endif
