#pragma once

#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/status.h"

typedef enum
{
    UID_GEN1A = 1,
    UID_GEN1B,
    UID_GEN2,
    UID_FUID,
    UID_GEN3,
    UID_GEN4_GTU,
    UID_GDM_AUTH,
    UID_GDM_20,
    UID_GDM_40,
    UID_GDM_ZUID,
    UID_SUPER_GEN1,
} mifare_uid_method_t;

typedef struct
{
    mifare_uid_method_t method;
    nfca_tag_t expected;
    uint8_t uid[7];
    bool key_b;
    const uint8_t *key; /* Borrowed six-byte key, required for Gen2/FUID/GDM. */
    const uint8_t *password; /* Borrowed four-byte GTU password; NULL = zero. */
} mifare_uid_request_t;

/* Explicit one-shot physical UID mutation. Initialized IDLE required.
 * UID length remains 4 or 7 bytes. Retains block-0 metadata, recomputes BCC.
 * Releases acquired RF before returning; rejection preserves other roles.
 * No retries after a mutation; a failed response
 * can mean an unknown write outcome. Success verifies the new activation.
 * Request and credentials are borrowed only for this call. */
#ifdef __cplusplus
extern "C"
{
#endif
    /* Credential-aware, read-only GTU configuration probe. No write data.
     * Same ownership and cleanup rules; requires GTU method and no key. */
    nfc_status_t nfca_mifare_uid_probe(const mifare_uid_request_t *request);
    nfc_status_t nfca_mifare_set_uid(const mifare_uid_request_t *request);

#ifdef __cplusplus
}
#endif
