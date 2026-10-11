#pragma once

#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/nfca_frame.h"

#define NTAG_MAX_PAGES 231u
#define NTAG_PAGE_BYTES 4u
#define NTAG_READ_PAGES 4u
#define NTAG_UID_BYTES NFCA_UID_DOUBLE_BYTES
#define NTAG_VERSION_BYTES NFCA_VERSION_BYTES
#define NTAG_SIGNATURE_BYTES NFCA_SIGNATURE_BYTES
#define NTAG_COUNTER_BYTES NFCA_COUNTER_BYTES
#define NTAG_PACK_BYTES NFCA_PACK_BYTES
#define NTAG_OPCODE_BYTES 1u
#define NTAG_INDEXED_COMMAND_BYTES 2u
#define NTAG_FAST_READ_COMMAND_BYTES 3u
#define NTAG_WRITE_COMMAND_BYTES (NTAG_INDEXED_COMMAND_BYTES + NTAG_PAGE_BYTES)
#define NTAG_COUNTER_UPDATE_COMMAND_BYTES 6u
#define NTAG_STATIC_LOCK_PAGE 2u
#define NTAG_STATIC_LOCK_BYTE 2u
#define NTAG_STATIC_LOCK_FIRST_PAGE 3u
#define NTAG_STATIC_LOCK_LAST_PAGE 15u
#define NTAG_OTP_PAGE 3u
#define NTAG_DYNAMIC_LOCK_FIRST_PAGE 16u
#define NTAG_DYNAMIC_LOCK_BITS 16u
#define NTAG_DOUBLE_PAGE_LOCK_GROUP_PAGES 2u
#define NTAG21X_LOCK_GROUP_PAGES 16u
#define NTAG21X_COUNTER_INDEX 2u
#define NTAG_COUNTER_MAX 0xffffffu
#define NTAG_TEARING_OK 0xbdu

/* Page offsets relative to config_page. */
#define NTAG_CONFIG_ACCESS_PAGE 1u
#define NTAG_CONFIG_PASSWORD_PAGE 2u
#define NTAG_CONFIG_PACK_PAGE 3u
#define NTAG_CONFIG_AUTH0_BYTE 3u
#define NTAG_CONFIG_ACCESS_BYTE 0u
#define NTAG_CONFIG_VCTID_BYTE 1u
#define NTAG_ACCESS_AUTHLIM_MASK 0x07u
#define NTAG_ACCESS_NFC_CNT_PWD_PROT 0x08u
#define NTAG_ACCESS_NFC_CNT_EN 0x10u
#define NTAG_ACCESS_CFGLCK 0x40u
#define NTAG_ACCESS_PROT 0x80u

typedef struct
{
    card_model_t model;
    uint8_t page_count;
    uint8_t config_page;
    uint8_t dynamic_lock_page;
    uint8_t dynamic_lock_granularity;
    uint16_t dynamic_lock_mask;
    uint8_t dynamic_block_lock_mask;
    uint8_t version[NTAG_VERSION_BYTES];
    bool ev1;
    bool is_ntag21x;
} ntag_config_t;

/* Pure model lookup, NULL for an unrelated model. Loaded configuration is
 * copied into the backend context before RX starts. */
const ntag_config_t *ntag_model_config(card_model_t model);

static inline bool ntag_counter_index_is_valid(uint8_t counter)
{
    return counter < NTAG_COUNTER_COUNT;
}
