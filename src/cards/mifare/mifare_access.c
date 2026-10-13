#include "cards/mifare/mifare_access.h"

#include "common/compiler.h"
#include "pn532_unleashed/nfca_frame.h"

#include <limits.h>
#include <stddef.h>

#define MIFARE_ACCESS_CONDITION_DENIED 7u
#define MIFARE_DATA_ALL_OPERATIONS                                             \
    (MIFARE_DATA_READ | MIFARE_DATA_WRITE | MIFARE_DATA_INCREMENT |            \
     MIFARE_DATA_DECREMENT)

static const uint8_t data_access[][2] RAM_DATA("mifare_access") = {
    {MIFARE_DATA_ALL_OPERATIONS, MIFARE_DATA_ALL_OPERATIONS},
    {MIFARE_DATA_READ, MIFARE_DATA_READ | MIFARE_DATA_WRITE},
    {MIFARE_DATA_READ, MIFARE_DATA_READ},
    {MIFARE_DATA_READ | MIFARE_DATA_DECREMENT, MIFARE_DATA_ALL_OPERATIONS},
    {MIFARE_DATA_READ | MIFARE_DATA_DECREMENT,
     MIFARE_DATA_READ | MIFARE_DATA_DECREMENT},
    {0u, MIFARE_DATA_READ},
    {0u, MIFARE_DATA_READ | MIFARE_DATA_WRITE},
    {0u, 0u},
};

/* NXP MF1S50YYX_V1 table 7; index is C3:C2:C1, not printed C1:C2:C3. */
static const uint8_t trailer_access[][2] RAM_DATA("mifare_access") = {
    {MIFARE_TRAILER_WRITE_KEY_A | MIFARE_TRAILER_READ_ACCESS |
         MIFARE_TRAILER_READ_KEY_B | MIFARE_TRAILER_WRITE_KEY_B,
     0u},
    {MIFARE_TRAILER_READ_ACCESS,
     MIFARE_TRAILER_WRITE_KEY_A | MIFARE_TRAILER_READ_ACCESS |
         MIFARE_TRAILER_WRITE_KEY_B},
    {MIFARE_TRAILER_READ_ACCESS | MIFARE_TRAILER_READ_KEY_B, 0u},
    {MIFARE_TRAILER_READ_ACCESS, MIFARE_TRAILER_READ_ACCESS},
    {MIFARE_TRAILER_WRITE_KEY_A | MIFARE_TRAILER_READ_ACCESS |
         MIFARE_TRAILER_WRITE_ACCESS | MIFARE_TRAILER_READ_KEY_B |
         MIFARE_TRAILER_WRITE_KEY_B,
     0u},
    {MIFARE_TRAILER_READ_ACCESS,
     MIFARE_TRAILER_READ_ACCESS | MIFARE_TRAILER_WRITE_ACCESS},
    {MIFARE_TRAILER_READ_ACCESS,
     MIFARE_TRAILER_WRITE_KEY_A | MIFARE_TRAILER_READ_ACCESS |
         MIFARE_TRAILER_WRITE_ACCESS | MIFARE_TRAILER_WRITE_KEY_B},
    {MIFARE_TRAILER_READ_ACCESS, MIFARE_TRAILER_READ_ACCESS},
};

void mifare_access_init(mifare_access_t *access,
                        uint8_t (*memory)[MIFARE_BLOCK_SIZE],
                        uint16_t block_count)
{
    access->memory = memory;
    access->block_count = block_count;
    mifare_access_reset(access);
}

bool RAM_FUNC(mifare_trailer_access_bits_valid)(
    const uint8_t trailer[MIFARE_BLOCK_SIZE])
{
    if (trailer == NULL)
    {
        return false;
    }
    const uint8_t access0 = trailer[MIFARE_TRAILER_ACCESS_BYTE];
    const uint8_t access1 = trailer[MIFARE_TRAILER_ACCESS_BYTE + 1u];
    const uint8_t access2 = trailer[MIFARE_TRAILER_ACCESS_BYTE + 2u];
    const uint8_t inverted0 = (uint8_t) ~((access0 >> 4u) | (access0 << 4u));
    const uint8_t inverted1 = (uint8_t) ~((access1 >> 4u) | (access1 << 4u));
    return ((inverted0 ^ access1) & 0xf0u) == 0u &&
           ((inverted0 ^ access2) & 0x0fu) == 0u &&
           ((inverted1 ^ access2) & 0xf0u) == 0u;
}

static uint8_t
RAM_FUNC(trailer_access_condition)(const uint8_t trailer[MIFARE_BLOCK_SIZE],
                                   uint8_t group)
{
    if (!mifare_trailer_access_bits_valid(trailer))
    {
        return MIFARE_ACCESS_CONDITION_DENIED;
    }
    const uint8_t c1 =
        (uint8_t)((~trailer[MIFARE_TRAILER_ACCESS_BYTE] >> group) & 1u);
    const uint8_t c2 =
        (uint8_t)((trailer[MIFARE_TRAILER_ACCESS_BYTE + 2u] >> group) & 1u);
    const uint8_t c3 =
        (uint8_t)((trailer[MIFARE_TRAILER_ACCESS_BYTE + 2u] >> (group + 4u)) &
                  1u);
    return (uint8_t)((c3 << 2u) | (c2 << 1u) | c1);
}

static bool
RAM_FUNC(trailer_key_b_is_readable)(const uint8_t trailer[MIFARE_BLOCK_SIZE])
{
    return mifare_trailer_access_bits_valid(trailer) &&
           (trailer_access[trailer_access_condition(
                trailer, MIFARE_TRAILER_ACCESS_GROUP)][MIFARE_KEY_A] &
            MIFARE_TRAILER_READ_KEY_B) != 0u;
}

uint8_t mifare_trailer_key_permissions(const uint8_t trailer[16], uint8_t key)
{
    if (key > MIFARE_KEY_B || !mifare_trailer_access_bits_valid(trailer) ||
        (key == MIFARE_KEY_B && trailer_key_b_is_readable(trailer)))
    {
        return 0u;
    }
    return trailer_access[trailer_access_condition(
        trailer, MIFARE_TRAILER_ACCESS_GROUP)][key];
}

static uint8_t access_group(uint8_t block)
{
    if (block < MIFARE_LARGE_SECTOR_FIRST_BLOCK)
    {
        return (uint8_t)(block & (MIFARE_SMALL_SECTOR_BLOCKS - 1u));
    }
    const uint8_t block_in_sector =
        (uint8_t)(block & (MIFARE_LARGE_SECTOR_BLOCKS - 1u));
    return block_in_sector == MIFARE_LARGE_SECTOR_BLOCKS - 1u
               ? MIFARE_TRAILER_ACCESS_GROUP
               : (uint8_t)(block_in_sector / MIFARE_LARGE_ACCESS_GROUP_BLOCKS);
}

static uint8_t RAM_FUNC(decode_access_condition)(const mifare_access_t *access,
                                                 uint8_t block)
{
    const uint8_t *trailer =
        access->memory[mifare_sector_trailer(mifare_block_sector(block))];
    return trailer_access_condition(trailer, access_group(block));
}

uint8_t RAM_FUNC(mifare_trailer_permissions)(const mifare_access_t *access,
                                             uint8_t block)
{
    return trailer_access[decode_access_condition(access, block)]
                         [access->authenticated_key];
}

static bool RAM_FUNC(key_b_is_readable)(const mifare_access_t *access,
                                        uint8_t sector)
{
    const uint8_t block = mifare_sector_trailer(sector);
    return trailer_key_b_is_readable(access->memory[block]);
}

bool RAM_FUNC(mifare_authenticated_for_block)(const mifare_access_t *access,
                                              uint8_t block)
{
    if (!mifare_block_valid(access, block) ||
        mifare_block_sector(block) != access->authenticated_sector)
    {
        return false;
    }
    return access->authenticated_key != MIFARE_KEY_B ||
           !key_b_is_readable(access, access->authenticated_sector);
}

bool mifare_data_permission(const uint8_t trailer[16],
                            uint8_t block,
                            uint8_t key,
                            uint8_t operation)
{
    if (key > MIFARE_KEY_B || !mifare_trailer_access_bits_valid(trailer) ||
        mifare_is_sector_trailer(block) ||
        (key == MIFARE_KEY_B && trailer_key_b_is_readable(trailer)))
    {
        return false;
    }
    return (data_access[trailer_access_condition(trailer, access_group(block))]
                       [key] &
            operation) != 0u;
}

bool RAM_FUNC(mifare_data_operation_allowed)(const mifare_access_t *access,
                                             uint8_t block,
                                             uint8_t operation)
{
    if (!mifare_authenticated_for_block(access, block) ||
        mifare_is_sector_trailer(block))
    {
        return false;
    }
    return (data_access[decode_access_condition(access, block)]
                       [access->authenticated_key] &
            operation) != 0u;
}

void mifare_access_update_cache(mifare_access_t *access)
{
    const uint8_t sector = access->authenticated_sector;
    if (sector >= MIFARE_MAX_SECTORS)
    {
        access->first_block = UINT8_MAX;
        access->last_block = 0x00u;
        access->read_allowed_mask = 0u;
        access->trailer_permissions = 0u;
        return;
    }

    const uint8_t first_block = mifare_sector_first_block(sector);
    const uint8_t last_block = mifare_sector_trailer(sector);

    access->first_block = first_block;
    access->last_block = last_block;

    if (first_block >= access->block_count || last_block >= access->block_count)
    {
        access->read_allowed_mask = 0u;
        access->trailer_permissions = 0u;
        return;
    }

    const uint8_t *trailer = access->memory[last_block];
    const uint8_t key = access->authenticated_key;
    const uint8_t trailer_cond =
        trailer_access_condition(trailer, MIFARE_TRAILER_ACCESS_GROUP);

    if (key == MIFARE_KEY_B && trailer_key_b_is_readable(trailer))
    {
        access->read_allowed_mask = 0u;
        access->trailer_permissions = 0u;
        return;
    }

    access->trailer_permissions = trailer_access[trailer_cond][key];

    uint16_t mask = 0u;
    const uint8_t count = (uint8_t)(last_block - first_block + 1u);
    for (uint8_t i = 0u; i < count; i++)
    {
        const uint8_t blk = (uint8_t)(first_block + i);
        if (blk == last_block)
        {
            mask |= (uint16_t)(1u << i);
            continue;
        }
        const uint8_t grp = access_group(blk);
        const uint8_t cond = trailer_access_condition(trailer, grp);
        if ((data_access[cond][key] & MIFARE_DATA_READ) != 0u)
        {
            mask |= (uint16_t)(1u << i);
        }
    }
    access->read_allowed_mask = mask;
}
