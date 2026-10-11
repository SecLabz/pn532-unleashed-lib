#include "cards/type2/ntag_internal.h"
#include "common/endian.h"

#include <string.h>
bool ntag_access_allowed(uint8_t page, bool write)
{
    if (!ntag.config.ev1)
    {
        return true;
    }
    const uint8_t *config0 = ntag.memory[ntag.config.config_page];
    const uint8_t access =
        ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                   [NTAG_CONFIG_ACCESS_BYTE];
    if (write && ntag.config_locked &&
        (page == ntag.config.config_page ||
         page == ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE))
    {
        return false;
    }
    return ntag.authenticated || page < config0[NTAG_CONFIG_AUTH0_BYTE] ||
           (!write && (access & NTAG_ACCESS_PROT) == 0u);
}

bool ntag_page_locked(uint8_t page)
{
    if (page >= NTAG_STATIC_LOCK_FIRST_PAGE &&
        page <= NTAG_STATIC_LOCK_LAST_PAGE)
    {
        const uint16_t locks = read_le16(
            &ntag.memory[NTAG_STATIC_LOCK_PAGE][NTAG_STATIC_LOCK_BYTE]);
        if ((locks & (uint16_t)(1u << page)) != 0u)
        {
            return true;
        }
    }
    if (ntag.config.dynamic_lock_page == 0u ||
        page < NTAG_DYNAMIC_LOCK_FIRST_PAGE ||
        page >= ntag.config.dynamic_lock_page)
    {
        return false;
    }
    /* Supported groups are two or sixteen pages; avoid a software divide
     * in the response path. */
    const uint8_t shift = ntag.config.dynamic_lock_granularity ==
                                  NTAG_DOUBLE_PAGE_LOCK_GROUP_PAGES
                              ? 1u
                              : 4u;
    const uint8_t bit =
        (uint8_t)((page - NTAG_DYNAMIC_LOCK_FIRST_PAGE) >> shift);
    if (bit >= NTAG_DYNAMIC_LOCK_BITS)
    {
        return false;
    }
    const uint16_t locks =
        read_le16(&ntag.memory[ntag.config.dynamic_lock_page][0]);
    return (locks & (uint16_t)(1u << bit)) != 0u;
}

bool ntag_write_page(uint8_t page, const uint8_t data[NTAG_PAGE_BYTES])
{
    if (page >= ntag.config.page_count || page < NTAG_STATIC_LOCK_PAGE ||
        ntag_page_locked(page) || !ntag_access_allowed(page, true))
    {
        return false;
    }
    if (page == NTAG_STATIC_LOCK_PAGE)
    {
        uint8_t *lock_bytes = &ntag.memory[page][NTAG_STATIC_LOCK_BYTE];
        const uint16_t locks = read_le16(lock_bytes);
        uint16_t writable = 0xffffu;
        /* Existing BL bits freeze L bits; a simultaneous L/BL write is valid.
         */
        if ((locks & 0x0001u) != 0u)
        {
            writable &= (uint16_t)~0x0008u;
        }
        if ((locks & 0x0002u) != 0u)
        {
            writable &= (uint16_t)~0x03f0u;
        }
        if ((locks & 0x0004u) != 0u)
        {
            writable &= (uint16_t)~0xfc00u;
        }
        write_le16(lock_bytes,
                   locks |
                       (read_le16(&data[NTAG_STATIC_LOCK_BYTE]) & writable));
        return true;
    }
    if (page == NTAG_OTP_PAGE)
    {
        for (uint8_t i = 0u; i < NTAG_PAGE_BYTES; i++)
        {
            ntag.memory[NTAG_OTP_PAGE][i] |= data[i];
        }
        return true;
    }
    if (page == ntag.config.dynamic_lock_page && page != 0u)
    {
        const uint16_t locks = read_le16(ntag.memory[page]);
        const uint8_t block_locks =
            ntag.memory[page][2] & ntag.config.dynamic_block_lock_mask;
        uint16_t writable = ntag.config.dynamic_lock_mask;
        /* Each BL freezes two L bits. Seven groups cover all supported tags. */
        for (uint8_t group = 0u; group < 7u; group++)
        {
            if ((block_locks & (1u << group)) != 0u)
            {
                writable &= (uint16_t) ~(3u << (group * 2u));
            }
        }
        write_le16(ntag.memory[page], locks | (read_le16(data) & writable));
        ntag.memory[page][2] |= data[2] & ntag.config.dynamic_block_lock_mask;
        return true;
    }
    memcpy(ntag.memory[page], data, NTAG_PAGE_BYTES);
    return true;
}

bool ntag_read_page(uint8_t page, uint8_t out[NTAG_PAGE_BYTES])
{
    if (!ntag_access_allowed(page, false))
    {
        return false;
    }
    if (ntag.config.ev1 &&
        (page == ntag.config.config_page + NTAG_CONFIG_PASSWORD_PAGE ||
         page == ntag.config.config_page + NTAG_CONFIG_PACK_PAGE))
    {
        memset(out, 0, NTAG_PAGE_BYTES);
        return true;
    }
    memcpy(out, ntag.memory[page], NTAG_PAGE_BYTES);
    return true;
}
