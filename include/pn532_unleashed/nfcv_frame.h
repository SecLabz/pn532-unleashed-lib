#pragma once

#include "pn532_unleashed/integrity.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NFCV_UID_SIZE 8u
#define NFCV_CRC_SIZE NFC_CRC_BYTES
#define NFCV_FRAME_MAX_BYTES 64u
/* Flags, 64 four-byte blocks with security bytes, and CRC. */
#define NFCV_RESPONSE_MAX_BYTES 323u

#define NFCV_REQ_SUBCARRIER_TWO 0x01u
#define NFCV_REQ_DATARATE_HIGH 0x02u
#define NFCV_REQ_INVENTORY 0x04u
#define NFCV_REQ_PROTOCOL_EXT 0x08u
#define NFCV_REQ_SELECT 0x10u
#define NFCV_REQ_ADDRESS 0x20u
#define NFCV_REQ_OPTION 0x40u
#define NFCV_REQ_AFI 0x10u
#define NFCV_REQ_SLOT1 0x20u

#define NFCV_RES_OK 0x00u
#define NFCV_RES_ERROR 0x01u
#define NFCV_GUARD_TIME_US 310u

#define NFCV_ERR_CMD_NOT_SUPPORTED 0x01u
#define NFCV_ERR_CMD_NOT_RECOGNIZED 0x02u
#define NFCV_ERR_OPTION_NOT_SUPPORTED 0x03u
#define NFCV_ERR_GENERIC 0x0fu
#define NFCV_ERR_BLOCK_UNAVAILABLE 0x10u
#define NFCV_ERR_BLOCK_ALREADY_LOCKED 0x11u
#define NFCV_ERR_BLOCK_LOCKED 0x12u

#define NFCV_CMD_INVENTORY 0x01u
#define NFCV_CMD_STAY_QUIET 0x02u
#define NFCV_CMD_READ_SINGLE 0x20u
#define NFCV_CMD_WRITE_SINGLE 0x21u
#define NFCV_CMD_LOCK_BLOCK 0x22u
#define NFCV_CMD_READ_MULTIPLE 0x23u
#define NFCV_CMD_WRITE_MULTIPLE 0x24u
#define NFCV_CMD_SELECT 0x25u
#define NFCV_CMD_RESET_TO_READY 0x26u
#define NFCV_CMD_WRITE_AFI 0x27u
#define NFCV_CMD_LOCK_AFI 0x28u
#define NFCV_CMD_WRITE_DSFID 0x29u
#define NFCV_CMD_LOCK_DSFID 0x2au
#define NFCV_CMD_GET_SYSTEM_INFO 0x2bu
#define NFCV_CMD_GET_BLOCK_SECURITY 0x2cu

#define NFCV_CMD_CUSTOM_MIN 0xa0u
#define NFCV_CMD_CUSTOM_MAX 0xdfu

#define NFCV_CMD_NXP_SET_EAS 0xa2u
#define NFCV_CMD_NXP_RESET_EAS 0xa3u
#define NFCV_CMD_NXP_LOCK_EAS 0xa4u
#define NFCV_CMD_NXP_EAS_ALARM 0xa5u
#define NFCV_CMD_NXP_PASSWORD_PROTECT_EAS_AFI 0xa6u
#define NFCV_CMD_NXP_GET_SYSTEM_INFO 0xabu
#define NFCV_CMD_NXP_GET_RANDOM_NUMBER 0xb2u
#define NFCV_CMD_NXP_SET_PASSWORD 0xb3u
#define NFCV_CMD_NXP_WRITE_PASSWORD 0xb4u
#define NFCV_CMD_NXP_DESTROY 0xb9u
#define NFCV_CMD_NXP_ENABLE_PRIVACY 0xbau
#define NFCV_CMD_NXP_READ_SIGNATURE 0xbdu

#define NFCV_UID_PREFIX 0xe0u
#define NFCV_MANUFACTURER_NXP 0x04u
#define SLIX_PRODUCT_CODE 0x01u
#define SLIX_L_PRODUCT_CODE 0x03u
#define SLIX_BLOCK_COUNT 28u
#define SLIX_L_BLOCK_COUNT 8u
#define NFCV_SYSINFO_DSFID 0x01u
#define NFCV_SYSINFO_AFI 0x02u
#define NFCV_SYSINFO_MEMORY 0x04u
#define NFCV_SYSINFO_IC_REF 0x08u
#define NFCV_SYSINFO_BLOCK_SIZE_MASK 0x1fu
#define NFCV_REQUEST_HEADER_BYTES 2u
#define NFCV_INVENTORY_HEADER_BYTES 2u
#define NFCV_SYSINFO_FIELDS_ALL                                                \
    (NFCV_SYSINFO_DSFID | NFCV_SYSINFO_AFI | NFCV_SYSINFO_MEMORY |             \
     NFCV_SYSINFO_IC_REF)
    /* GET_SYSTEM_INFO payload with all four optional fields present, without
     * CRC. */
    enum
    {
        NFCV_SYSINFO_FLAGS_BYTE = 1,
        NFCV_SYSINFO_UID_BYTE,
        NFCV_SYSINFO_DSFID_BYTE = NFCV_SYSINFO_UID_BYTE + NFCV_UID_SIZE,
        NFCV_SYSINFO_AFI_BYTE,
        NFCV_SYSINFO_BLOCK_COUNT_BYTE,
        NFCV_SYSINFO_BLOCK_SIZE_BYTE,
        NFCV_SYSINFO_IC_REF_BYTE,
        NFCV_SYSINFO_PAYLOAD_BYTES,
    };

/* NXP SLIX password identifiers */
#define SLIX_PASSWORD_READ 0x01u
#define SLIX_PASSWORD_WRITE 0x02u
#define SLIX_PASSWORD_PRIVACY 0x04u
#define SLIX_PASSWORD_DESTROY 0x08u
#define SLIX_PASSWORD_EAS_AFI 0x10u

    static inline bool slix_password_id_is_valid(uint8_t password_id)
    {
        switch (password_id)
        {
        case SLIX_PASSWORD_READ:
        case SLIX_PASSWORD_WRITE:
        case SLIX_PASSWORD_PRIVACY:
        case SLIX_PASSWORD_DESTROY:
        case SLIX_PASSWORD_EAS_AFI:
            return true;
        default:
            return false;
        }
    }

    /* Owned decoded request. size_bytes includes its two CRC bytes and must
     * not exceed the fixed capacity. CRC/rate/subcarrier observations come
     * from the decoder or are supplied explicitly by an offline caller. */
    typedef struct
    {
        uint8_t bytes[NFCV_FRAME_MAX_BYTES];
        uint16_t size_bytes;
        bool crc_ok;
        bool high_data_rate;
        bool single_subcarrier;
    } nfcv_frame_t;

    /*
     * Reverse an eight-byte display-order UID into eight output bytes.
     * Both pointers must be non-NULL and their ranges must not overlap.
     */
    void nfcv_uid_to_wire(uint8_t *dst, const uint8_t *uid);

    /*
     * Compare two non-NULL eight-byte UIDs in their respective orders.
     */
    bool nfcv_wire_uid_matches(const uint8_t *wire_uid, const uint8_t *uid);

    /*
     * Test a wire-order UID against a mask of 0..64 bits. wire_uid has eight
     * bytes; mask_value has mask_size_bytes bytes. Missing storage or an
     * oversized/short mask returns false. A zero-bit mask may be NULL.
     */
    bool nfcv_inventory_mask_matches(const uint8_t *wire_uid,
                                     uint8_t mask_bits,
                                     const uint8_t *mask_value,
                                     uint8_t mask_size_bytes);

#ifdef __cplusplus
}
#endif
