#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NFC_CRC_BYTES 2u
#define NFCA_CRC_INITIAL 0x6363u
#define NFCV_CRC_INITIAL 0xffffu
#define NFC_PARITY_NIBBLE_TRUTH_TABLE 0x6996u

    /*
     * Pure wire-integrity primitives; no RF, allocation, or state changes.
     * CRC input is readable for size_bytes bytes (NULL allowed only for
     * size_bytes zero). Append requires writable payload size_bytes + 2 bytes;
     * it adds little-endian CRC without changing payload. Check accepts the
     * total frame size and returns false for NULL, short frames, or a CRC
     * mismatch.
     */
    static inline __attribute__((always_inline)) uint8_t
    nfca_odd_parity_bit(uint8_t value)
    {
        value ^= (uint8_t)(value >> 4u);
        value &= 0x0fu;
        return (uint8_t)(((NFC_PARITY_NIBBLE_TRUTH_TABLE >> value) & 1u) ^ 1u);
    }

    /* Advance CRC-A by one byte; callers start with 0x6363. */
    static inline __attribute__((always_inline)) uint16_t
    nfca_crc16_update(uint16_t crc, uint8_t value)
    {
        uint8_t byte = (uint8_t)(value ^ crc);
        byte ^= (uint8_t)(byte << 4u);
        return (uint16_t)((crc >> 8u) ^ ((uint16_t)byte << 8u) ^
                          ((uint16_t)byte << 3u) ^ ((uint16_t)byte >> 4u));
    }

    static inline __attribute__((always_inline)) uint16_t
    nfca_crc16(const uint8_t *data, size_t size_bytes)
    {
        uint16_t crc = NFCA_CRC_INITIAL;
        for (size_t i = 0u; i < size_bytes; i++)
        {
            crc = nfca_crc16_update(crc, data[i]);
        }
        return crc;
    }

    static inline __attribute__((always_inline)) uint16_t
    nfcv_crc16(const uint8_t *data, size_t size_bytes)
    {
        static const uint16_t nfcv_crc16_table[] = {
            0x0000u,
            0x1081u,
            0x2102u,
            0x3183u,
            0x4204u,
            0x5285u,
            0x6306u,
            0x7387u,
            0x8408u,
            0x9489u,
            0xa50au,
            0xb58bu,
            0xc60cu,
            0xd68du,
            0xe70eu,
            0xf78fu,
        };
        uint16_t crc = NFCV_CRC_INITIAL;
        for (size_t i = 0u; i < size_bytes; i++)
        {
            crc ^= data[i];
            crc = (uint16_t)((crc >> 4u) ^ nfcv_crc16_table[crc & 0x0fu]);
            crc = (uint16_t)((crc >> 4u) ^ nfcv_crc16_table[crc & 0x0fu]);
        }
        return (uint16_t)~crc;
    }

    static inline void
    nfc_append_crc_le(uint8_t *data, size_t payload_size_bytes, uint16_t crc)
    {
        data[payload_size_bytes] = (uint8_t)crc;
        data[payload_size_bytes + 1u] = (uint8_t)(crc >> 8u);
    }

    static inline bool nfc_crc_le_matches(const uint8_t *data,
                                          size_t payload_size_bytes,
                                          uint16_t crc)
    {
        return data[payload_size_bytes] == (uint8_t)crc &&
               data[payload_size_bytes + 1u] == (uint8_t)(crc >> 8u);
    }

    static inline bool nfca_check_crc(const uint8_t *data,
                                      size_t frame_size_bytes)
    {
        if (data == NULL || frame_size_bytes < 1u + NFC_CRC_BYTES)
        {
            return false;
        }
        const size_t payload_size_bytes = frame_size_bytes - NFC_CRC_BYTES;
        return nfc_crc_le_matches(
            data, payload_size_bytes, nfca_crc16(data, payload_size_bytes));
    }

    static inline bool nfcv_check_crc(const uint8_t *data,
                                      size_t frame_size_bytes)
    {
        if (data == NULL || frame_size_bytes < NFC_CRC_BYTES)
        {
            return false;
        }
        const size_t payload_size_bytes = frame_size_bytes - NFC_CRC_BYTES;
        return nfc_crc_le_matches(
            data, payload_size_bytes, nfcv_crc16(data, payload_size_bytes));
    }

    static inline void nfca_append_crc(uint8_t *data, size_t size_bytes)
    {
        nfc_append_crc_le(data, size_bytes, nfca_crc16(data, size_bytes));
    }

    static inline void nfcv_append_crc(uint8_t *data, size_t size_bytes)
    {
        nfc_append_crc_le(data, size_bytes, nfcv_crc16(data, size_bytes));
    }

#ifdef __cplusplus
}
#endif
