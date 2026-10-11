#pragma once

#include "pn532_unleashed/integrity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* The complete NTAG215 memory (135 pages) plus CRC-A. */
#define NFCA_TAG_MAX_RESPONSE_BYTES 542u

    /*
     * Describes one decoded NFC-A request passed to a tag profile.
     * bytes is readable for size_bytes bytes during handling. parity may be
     * NULL for plaintext requests; otherwise it has size_bytes parity entries.
     * short_frame represents the supported seven-bit REQA/WUPA requests.
     * timestamp_us is the wrapping local microsecond clock for HALT state.
     */
    typedef struct
    {
        const uint8_t *bytes;
        const uint8_t *parity;
        uint16_t size_bytes;
        bool short_frame;
        bool parity_ok;
        bool crc_ok;
        uint32_t timestamp_us;
    } nfca_tag_request_t;

    typedef enum
    {
        NFCA_TAG_RESPONSE_NONE = 0,
        NFCA_TAG_RESPONSE_DATA,
        NFCA_TAG_RESPONSE_DATA_WITH_PARITY,
        NFCA_TAG_RESPONSE_SHORT4,
    } nfca_tag_response_type_t;

    /* Owned decoded response. size_bytes counts complete bytes including any
     * CRC; parity has size_bytes entries only for DATA_WITH_PARITY. SHORT4 uses
     * the low four bits of short4. Only bytes[0:size_bytes] are meaningful for
     * DATA. No PHY scheduling, capture samples, or transmit producers are
     * exposed. */
    typedef struct
    {
        nfca_tag_response_type_t type;
        uint8_t bytes[NFCA_TAG_MAX_RESPONSE_BYTES];
        uint8_t parity[NFCA_TAG_MAX_RESPONSE_BYTES];
        uint16_t size_bytes;
        uint8_t short4;
    } nfca_response_t;

    /* Response builders require non-NULL response. Failed data builders leave
     * it unchanged. Data is borrowed for the call and must not overlap output.
     * data() copies exact bytes; data_crc() appends CRC-A. Only control fields
     * are reset: payload/parity outside size_bytes is never meaningful. */
    static inline void nfca_response_silent(nfca_response_t *response)
    {
        response->type = NFCA_TAG_RESPONSE_NONE;
        response->size_bytes = 0u;
        response->short4 = 0u;
    }

    static inline void nfca_response_short4(nfca_response_t *response,
                                            uint8_t value)
    {
        response->type = NFCA_TAG_RESPONSE_SHORT4;
        response->size_bytes = 0u;
        response->short4 = (uint8_t)(value & 0x0fu);
    }

    static inline bool nfca_response_data(nfca_response_t *response,
                                          const uint8_t *data,
                                          uint16_t size_bytes)
    {
        if (data == NULL || size_bytes == 0u ||
            size_bytes > NFCA_TAG_MAX_RESPONSE_BYTES)
        {
            return false;
        }
        memcpy(response->bytes, data, size_bytes);
        response->type = NFCA_TAG_RESPONSE_DATA;
        response->size_bytes = size_bytes;
        response->short4 = 0u;
        return true;
    }

    static inline bool nfca_response_data_crc(nfca_response_t *response,
                                              const uint8_t *data,
                                              uint16_t size_bytes)
    {
        if (size_bytes > NFCA_TAG_MAX_RESPONSE_BYTES - NFC_CRC_BYTES ||
            !nfca_response_data(response, data, size_bytes))
        {
            return false;
        }
        nfca_append_crc(response->bytes, size_bytes);
        response->size_bytes += NFC_CRC_BYTES;
        return true;
    }

#define NFCA_SHORT_REQUEST_BITS 7u
#define NFCA_SHORT_RESPONSE_BITS 4u
#define NFCA_SHORT_RESPONSE_MASK ((1u << NFCA_SHORT_RESPONSE_BITS) - 1u)
#define NFCA_UID_SINGLE_BYTES 4u
#define NFCA_UID_DOUBLE_BYTES 7u
#define NFCA_UID_TRIPLE_BYTES 10u
#define NFCA_ATQA_BYTES 2u
#define NFCA_ATQA_UID_SIZE_MASK 0xc0u
#define NFCA_ATQA_UID_SINGLE 0x00u
#define NFCA_ATQA_UID_DOUBLE 0x40u
#define NFCA_ATQA_UID_TRIPLE 0x80u
#define NFCA_CASCADE_LEVELS 3u
#define NFCA_UID_FRAGMENT_BYTES 4u
#define NFCA_ANTICOLLISION_BYTES (NFCA_UID_FRAGMENT_BYTES + 1u)
#define NFCA_CASCADE_UID_BYTES (NFCA_UID_FRAGMENT_BYTES - 1u)
#define NFCA_COMMAND_HEADER_BYTES 2u
#define NFCA_SELECT_BYTES                                                      \
    (NFCA_COMMAND_HEADER_BYTES + NFCA_ANTICOLLISION_BYTES + NFC_CRC_BYTES)
#define NFCA_SAK_FRAME_BYTES (1u + NFC_CRC_BYTES)
#define NFCA_HALT_FRAME_BYTES (NFCA_COMMAND_HEADER_BYTES + NFC_CRC_BYTES)

#define NFCA_CMD_REQA 0x26u
#define NFCA_CMD_WUPA 0x52u
#define ISODEP_CMD_RATS 0xe0u
#define ISODEP_CMD_PPS 0xd0u

#define NFCA_CMD_ANTICOLL_CL1 0x93u
#define NFCA_CMD_ANTICOLL_CL2 0x95u
#define NFCA_CMD_ANTICOLL_CL3 0x97u
#define NFCA_CMD_HALT 0x50u
#define NFCA_CMD_READ 0x30u
#define NFCA_CMD_RATS ISODEP_CMD_RATS
#define NFCA_CMD_PPS ISODEP_CMD_PPS
#define NFCA_SELECT_NVB 0x70u
#define NFCA_ANTICOLLISION_NVB 0x20u
#define NFCA_CASCADE_TAG 0x88u
#define NFCA_SAK_CASCADE 0x04u
#define NFCA_SAK_ISODEP 0x20u
#define ISODEP_PCB_I_BLOCK_0 0x02u
#define ISODEP_PCB_I_BLOCK_1 0x03u

/* 4-bit cryptographic and protocol acknowledgements (ISO/IEC 14443-3 / MIFARE /
 * NTAG) */
#define NFCA_ACK 0x0au
#define NFCA_NACK_INVALID 0x00u
#define NFCA_NACK_CRC 0x01u
#define NFCA_NACK_AUTH 0x04u
#define NFCA_NACK_EEPROM 0x05u

#define MIFARE_SAK_1K 0x08u
    /*
     * Calculates the BCC for one four-byte NFC-A anticollision fragment.
     */
    static inline uint8_t nfca_bcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        return (uint8_t)(a ^ b ^ c ^ d);
    }

    static inline bool
    nfca_bcc_valid(const uint8_t uid_bcc[NFCA_ANTICOLLISION_BYTES])
    {
        return nfca_bcc(uid_bcc[0], uid_bcc[1], uid_bcc[2], uid_bcc[3]) ==
               uid_bcc[NFCA_UID_FRAGMENT_BYTES];
    }

    static inline void nfca_add_bcc(uint8_t uid_bcc[NFCA_ANTICOLLISION_BYTES])
    {
        uid_bcc[NFCA_UID_FRAGMENT_BYTES] =
            nfca_bcc(uid_bcc[0], uid_bcc[1], uid_bcc[2], uid_bcc[3]);
    }

#define MIFARE_KEY_BYTES 6u
#define MIFARE_NONCE_BYTES 4u
#define MIFARE_COMMAND_BYTES 2u
#define MIFARE_READER_AUTH_BYTES (2u * MIFARE_NONCE_BYTES)

#define MIFARE_CMD_AUTH_A 0x60u
#define MIFARE_CMD_AUTH_B 0x61u
#define MIFARE_CMD_READ 0x30u
#define MIFARE_CMD_WRITE 0xa0u
#define MIFARE_CMD_TRANSFER 0xb0u
#define MIFARE_CMD_DECREMENT 0xc0u
#define MIFARE_CMD_INCREMENT 0xc1u
#define MIFARE_CMD_RESTORE 0xc2u

#define MIFARE_ACK NFCA_ACK
#define MIFARE_NACK_INVALID NFCA_NACK_INVALID
#define MIFARE_NACK_CRC NFCA_NACK_CRC
#define MIFARE_NACK_NOT_AUTHENTICATED NFCA_NACK_AUTH
#define MIFARE_NACK_OTHER 0x06u

#define NTAG_PASSWORD_BYTES 4u

#define NTAG_CMD_PWD_AUTH 0x1bu
#define NTAG_CMD_READ 0x30u
#define NTAG_CMD_READ_CNT 0x39u
#define NTAG_CMD_FAST_READ 0x3au
#define NTAG_CMD_READ_SIG 0x3cu
#define NTAG_CMD_CHECK_TEARING 0x3eu
#define NTAG_CMD_VCSL 0x4bu
#define NTAG_CMD_GET_VERSION 0x60u
#define NTAG_CMD_COMPAT_WRITE 0xa0u
#define NTAG_CMD_WRITE 0xa2u
#define NTAG_CMD_INCR_CNT 0xa5u

#define NTAG_NACK_INVALID NFCA_NACK_INVALID
#define NTAG_NACK_CRC NFCA_NACK_CRC
#define NTAG_NACK_AUTH NFCA_NACK_AUTH
#define NTAG_NACK_EEPROM NFCA_NACK_EEPROM

#define NTAG_COUNTER_COUNT 3u

#ifdef __cplusplus
}
#endif
