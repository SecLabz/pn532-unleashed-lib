#pragma once

#include "pn532_unleashed/status.h"

#include <stdint.h>

#define NFCV_UID_BYTES 8u
#define NFCV_SLIX_DUMP_MAX_BLOCKS 28u
#define NFCV_BLOCK_BYTES 4u
#define NFCV_SLIX_DUMP_MAX_MEMORY_BYTES                                        \
    (NFCV_SLIX_DUMP_MAX_BLOCKS * NFCV_BLOCK_BYTES)

/* Observed NFC-V identity and geometry. UID is in display order, beginning
 * E0. Scan accepts 1..255 blocks of 1..32 bytes; dump is limited to SLIX and
 * SLIX-L. No pointers to library storage. */
typedef struct
{
    uint8_t uid[NFCV_UID_BYTES];
    uint8_t dsfid;
    uint8_t afi;
    uint8_t ic_ref;
    uint8_t block_count;
    uint8_t block_size_bytes;
} nfcv_tag_t;

/* Complete readable SLIX/SLIX-L memory only. Valid only after NFC_OK;
 * slix_read resets memory_size_bytes to zero on every failure. security[i]
 * is the observed block
 * security byte (0 or 1). Only block_count security entries and block_count *
 * block_size_bytes memory bytes are meaningful; unused bytes are zero.
 * Passwords and unobservable lock state are not inferred. */
typedef struct
{
    nfcv_tag_t tag;
    uint16_t memory_size_bytes;
    uint8_t security[NFCV_SLIX_DUMP_MAX_BLOCKS];
    uint8_t memory[NFCV_SLIX_DUMP_MAX_MEMORY_BYTES];
} nfcv_slix_dump_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Discover one NFC-V tag with inventory and system information. Require
     * initialized IDLE; active RF returns BUSY. One attempt per exchange.
     * Accept 1..255 blocks of 1..32 bytes with all system-info fields present;
     * other geometries return UNSUPPORTED. UID must agree across responses.
     * Release acquired RF before returning; rejection preserves other roles.
     * Output is unchanged on failure. */
    nfc_status_t nfcv_scan(nfcv_tag_t *tag);

    /* Read complete observable SLIX (28) or SLIX-L (8) four-byte blocks and
     * security status. Require initialized IDLE; active RF returns BUSY.
     * Reject other identities/geometries before reading blocks. Retain three
     * attempts per exchange. Release acquired RF before returning; rejection
     * preserves other roles. Reset
     * memory_size_bytes on every failure; no partial dump is published. */
    nfc_status_t nfcv_slix_read(nfcv_slix_dump_t *dump);

#ifdef __cplusplus
}
#endif
