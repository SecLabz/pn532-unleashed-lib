#pragma once

#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/status.h"

typedef enum
{
    MAGIC_GEN1A = 1u << 0,
    MAGIC_GEN1B = 1u << 1,
    MAGIC_GEN2 = 1u << 2,
    MAGIC_UNFUSED = 1u << 3,
    MAGIC_SUPER_GEN1 = 1u << 4,
    MAGIC_SUPER_GEN2 = 1u << 5,
    MAGIC_GEN3 = 1u << 7,
    MAGIC_GEN4_GTU = 1u << 8,
    MAGIC_GDM_AUTH = 1u << 9,
    MAGIC_QL88 = 1u << 10,
    MAGIC_GDM_20 = 1u << 11,
    MAGIC_GDM_40 = 1u << 12,
    MAGIC_GDM_ZUID = 1u << 13,
} mifare_magic_flag_t;

typedef enum
{
    MAGIC_GEN2_NOT_CHECKED = 0,
    MAGIC_GEN2_AUTH_DENIED,
    MAGIC_GEN2_WRITE_DENIED,
    MAGIC_GEN2_WRITE_ACK,
    MAGIC_GEN2_ATS,
    MAGIC_GEN2_WRITE_INCONCLUSIVE,
} mifare_magic_gen2_t;

typedef struct
{
    uint16_t flags;
    mifare_magic_gen2_t gen2;
} mifare_magic_result_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Bounded foreground probes of a previously selected Classic identity.
     * Requires initialized IDLE. Probes reset RF; selections validate the same
     * UID/ATQA/SAK, including before publishing the result. Releases acquired
     * RF before returning; rejection preserves other roles. No pointers
     * retained; result changes only on success. A missing
     * signature is inconclusive. NULL key enables read-only probes. An explicit
     * six-byte key also permits one AUTH and the first WRITE phase for block 0;
     * RF is cut immediately after its reply, and no write data is ever sent.
     * UID/memory are unchanged. QL88 detection uses its fixed signature key,
     * not a dictionary search.
     */
    nfc_status_t nfca_mifare_magic(const nfca_tag_t *expected,
                                   const uint8_t key[MIFARE_KEY_BYTES],
                                   bool key_b,
                                   mifare_magic_result_t *result);

#ifdef __cplusplus
}
#endif
