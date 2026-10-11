#pragma once

#include "cards/type2/ntag_config.h"
#include "common/compiler.h"
#include "pn532_unleashed/nfca_activation.h"
#include "protocols/nfca/nfca_profile.h"

#define NTAG_MAX_FAST_READ_PAGES                                               \
    ((NFCA_TAG_MAX_RESPONSE_BYTES - NFC_CRC_BYTES) / NTAG_PAGE_BYTES)

typedef struct
{
    ntag_config_t config;
    nfca_activation_t activation;
    bool authenticated;
    bool config_locked;
    bool compat_write_pending;
    uint8_t compat_write_page;
    uint32_t auth_attempts;
    uint8_t memory[NTAG_MAX_PAGES][NTAG_PAGE_BYTES];
    uint8_t signature[NTAG_SIGNATURE_BYTES];
    uint32_t counters[NTAG_COUNTER_COUNT];
    uint8_t tearing[NTAG_COUNTER_COUNT];
    bool single_counter_increased;
} ntag_context_t;

/* One Type 2 backend, mutated only with RX stopped. */
extern ntag_context_t ntag;
bool ntag_access_allowed(uint8_t page, bool write);
bool ntag_page_locked(uint8_t page);
bool ntag_write_page(uint8_t page, const uint8_t data[NTAG_PAGE_BYTES]);
bool ntag_read_page(uint8_t page, uint8_t out[NTAG_PAGE_BYTES]);
