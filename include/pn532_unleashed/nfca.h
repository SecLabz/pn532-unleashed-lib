#pragma once

#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfca_frame.h"
#include "pn532_unleashed/status.h"

#include <stdbool.h>
#include <stdint.h>

#define NFCA_VERSION_BYTES 8u
#define NFCA_SIGNATURE_BYTES 32u
#define NFCA_PACK_BYTES 2u
#define NFCA_COUNTER_BYTES 3u

/* Reader-only PN532 receive channel and gain. Numeric values are stable
 * for host commands and saved reference-firmware profiles. */
typedef enum
{
    NFCA_RX_Q_33_DB = 0,
    NFCA_RX_I_18_DB = 1,
    NFCA_RX_I_33_DB = 2,
    NFCA_RX_Q_18_DB = 3,
} nfca_rx_profile_t;

/* Selected physical tag identity. ATQA bytes retain RF order. Only the first
 * uid_size_bytes UID bytes are meaningful; remaining bytes are zero. */
typedef struct
{
    uint8_t uid[NFCA_UID_TRIPLE_BYTES];
    uint8_t uid_size_bytes;
    uint8_t atqa[NFCA_ATQA_BYTES];
    uint8_t sak;
} nfca_tag_t;

#define NFCA_MAX_MEMORY_BYTES 4096u
#define NFCA_METADATA_VERSION 0x01u
#define NFCA_METADATA_SIGNATURE 0x02u
#define NFCA_METADATA_PACK 0x04u

/* Type 2 observations, not an emulation image. Secret page bytes can be
 * masked by the physical tag. Valid only after NFC_OK; a failed read may
 * overwrite this caller-owned buffer. No RF engine borrows it afterward.
 * metadata_valid bits: 0 version, 1 signature, 2 PACK. Other hidden state,
 * including failed password attempts, is not readable and is not invented. */
typedef struct
{
    nfca_tag_t tag;
    uint16_t memory_size_bytes;
    uint8_t memory[NFCA_MAX_MEMORY_BYTES];
    uint8_t metadata_valid;
    uint8_t version[NFCA_VERSION_BYTES];
    uint8_t signature[NFCA_SIGNATURE_BYTES];
    uint8_t pack[NFCA_PACK_BYTES];
    /* Valid masks use bits 0..2. Counters are three little-endian RF bytes;
     * a missing bit means unknown, including disabled/protected counters. */
    uint8_t counter_valid;
    uint8_t counters_le[NTAG_COUNTER_COUNT][NFCA_COUNTER_BYTES];
    uint8_t tearing_valid;
    uint8_t tearing[NTAG_COUNTER_COUNT];
} nfca_dump_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Default Q/33 dB. Foreground on the RF owner core only. Setter returns
     * false for an invalid profile or active RF, leaving the choice intact.
     * Accepted changes only update RAM; applied at the next NFC-A reader
     * setup. Choice survives nfc_stop(), not MCU reset. NFC-V and emulation
     * are unaffected; flash persistence belongs to the application. */
    nfca_rx_profile_t nfca_reader_rx_profile(void);
    bool nfca_reader_set_rx_profile(nfca_rx_profile_t profile);

    /* Select an expected identity and explicit memory model from IDLE. Copies
     * identity/model; RF stays on until nfc_stop. Rejected arguments/BUSY leave
     * other roles untouched; selection failure releases acquired RF. */
    nfc_status_t nfca_select(const nfca_tag_t *expected, card_model_t model);
    /* Read all observable Type 2 pages and available metadata in a selected
     * session. Password borrows four bytes, or NULL; plain Ultralight accepts
     * NULL only. Authentication may consume failed-password attempts; reads
     * may increment enabled counters. Success keeps RF selected. On failure
     * memory_size_bytes is zero; discard the dump. Ordinary failures release
     * selection. A NULL dump is rejected without changing RF ownership. */
    nfc_status_t nfca_read_all(const uint8_t *password, nfca_dump_t *dump);

    /* One four-byte page, including observable manufacturer/configuration
     * pages. Password borrows four bytes, or NULL for unauthenticated access.
     * writable is false when required permission state is unreadable; it
     * describes user-data permissions. Outputs change only on success. */
    nfc_status_t nfca_read_page(uint8_t page,
                                const uint8_t *password,
                                uint8_t data[4],
                                bool *writable);
    /* User data only. Compare before, check permissions, send once, verify.
     * CONTENT_CHANGED means no write. WRITE_OUTCOME_UNKNOWN forbids retry.
     * Any failed page/block operation releases the selected session. */
    nfc_status_t nfca_write_page(uint8_t page,
                                 const uint8_t *password,
                                 const uint8_t before[4],
                                 const uint8_t after[4]);

    /* Select one NFC-A tag at 106 kbit/s. Require initialized IDLE. The
     * acquired reader field is stopped before return; rejection preserves other
     * roles. Output is unchanged on failure.
     * One WUPA attempt, then bounded anticollision/selection. No silence
     * retry. Collisions and malformed physical responses are rejected. */
    nfc_status_t nfca_scan(nfca_tag_t *tag);

#ifdef __cplusplus
}
#endif
