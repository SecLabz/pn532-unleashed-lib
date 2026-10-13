#pragma once

#include "pn532_unleashed/nfca_frame.h"

#include <stdbool.h>
#include <stdint.h>

#define MIFARE_BLOCK_SIZE 16u
#define MIFARE_BLOCK_FRAME_BYTES (MIFARE_BLOCK_SIZE + NFC_CRC_BYTES)
#define MIFARE_COMMAND_FRAME_BYTES (MIFARE_COMMAND_BYTES + NFC_CRC_BYTES)
#define MIFARE_VALUE_BYTES 4u
#define MIFARE_VALUE_FRAME_BYTES (MIFARE_VALUE_BYTES + NFC_CRC_BYTES)
#define MIFARE_VALUE_INVERTED_BYTE MIFARE_VALUE_BYTES
#define MIFARE_VALUE_COPY_BYTE (2u * MIFARE_VALUE_BYTES)
#define MIFARE_VALUE_ADDRESS_BYTE (3u * MIFARE_VALUE_BYTES)
#define MIFARE_SMALL_SECTOR_BLOCKS 4u
#define MIFARE_LARGE_SECTOR_BLOCKS 16u
#define MIFARE_MAX_SECTORS 40u
#define MIFARE_TRAILER_KEY_B_BYTE 10u
#define MIFARE_TRAILER_ACCESS_BYTE MIFARE_KEY_BYTES
#define MIFARE_TRAILER_ACCESS_BYTES 3u
#define MIFARE_TRAILER_USER_BYTE 9u
#define MIFARE_TRAILER_ACCESS_GROUP 3u
#define MIFARE_LARGE_ACCESS_GROUP_BLOCKS 5u

enum
{
    MIFARE_KEY_A = 0u,
    MIFARE_KEY_B = 1u,
};

enum
{
    MIFARE_DATA_READ = 1u << 0,
    MIFARE_DATA_WRITE = 1u << 1,
    MIFARE_DATA_INCREMENT = 1u << 2,
    MIFARE_DATA_DECREMENT = 1u << 3,
};

enum
{
    MIFARE_TRAILER_WRITE_KEY_A = 1u << 0,
    MIFARE_TRAILER_READ_ACCESS = 1u << 1,
    MIFARE_TRAILER_WRITE_ACCESS = 1u << 2,
    MIFARE_TRAILER_READ_KEY_B = 1u << 3,
    MIFARE_TRAILER_WRITE_KEY_B = 1u << 4,
};

typedef struct
{
    uint8_t (*memory)[MIFARE_BLOCK_SIZE];
    uint16_t block_count;
    uint8_t authenticated_sector;
    uint8_t authenticated_key;
    uint8_t first_block;
    uint8_t last_block;
    uint16_t read_allowed_mask;
    uint8_t trailer_permissions;
} mifare_access_t;

#define MIFARE_LARGE_SECTOR_FIRST_BLOCK 128u
#define MIFARE_LARGE_SECTOR_FIRST 32u

void mifare_access_init(mifare_access_t *access,
                        uint8_t (*memory)[MIFARE_BLOCK_SIZE],
                        uint16_t block_count);
void mifare_access_update_cache(mifare_access_t *access);

static inline void mifare_access_reset(mifare_access_t *access)
{
    access->authenticated_sector = UINT8_MAX;
    access->authenticated_key = UINT8_MAX;
    access->first_block = UINT8_MAX;
    access->last_block = 0x00u;
    access->read_allowed_mask = 0u;
    access->trailer_permissions = 0u;
}

static inline void
mifare_access_authenticate(mifare_access_t *access, uint8_t sector, uint8_t key)
{
    access->authenticated_sector = sector;
    access->authenticated_key = key;
}

static inline uint8_t mifare_block_sector(uint8_t block)
{
    if (block < MIFARE_LARGE_SECTOR_FIRST_BLOCK)
    {
        return (uint8_t)(block / MIFARE_SMALL_SECTOR_BLOCKS);
    }
    return (uint8_t)(MIFARE_LARGE_SECTOR_FIRST +
                     (block - MIFARE_LARGE_SECTOR_FIRST_BLOCK) /
                         MIFARE_LARGE_SECTOR_BLOCKS);
}

static inline uint8_t mifare_sector_first_block(uint8_t sector)
{
    return sector < MIFARE_LARGE_SECTOR_FIRST
               ? (uint8_t)(sector * MIFARE_SMALL_SECTOR_BLOCKS)
               : (uint8_t)(MIFARE_LARGE_SECTOR_FIRST_BLOCK +
                           (sector - MIFARE_LARGE_SECTOR_FIRST) *
                               MIFARE_LARGE_SECTOR_BLOCKS);
}

static inline uint8_t mifare_sector_trailer(uint8_t sector)
{
    if (sector < MIFARE_LARGE_SECTOR_FIRST)
    {
        return (uint8_t)(sector * MIFARE_SMALL_SECTOR_BLOCKS +
                         MIFARE_SMALL_SECTOR_BLOCKS - 1u);
    }
    return (uint8_t)(MIFARE_LARGE_SECTOR_FIRST_BLOCK +
                     (sector - MIFARE_LARGE_SECTOR_FIRST) *
                         MIFARE_LARGE_SECTOR_BLOCKS +
                     MIFARE_LARGE_SECTOR_BLOCKS - 1u);
}

static inline bool mifare_block_valid(const mifare_access_t *access,
                                      uint8_t block)
{
    return (uint16_t)block < access->block_count;
}

static inline bool mifare_is_sector_trailer(uint8_t block)
{
    return block == mifare_sector_trailer(mifare_block_sector(block));
}

/* Pure trailer permissions; readable Key B cannot authenticate. */
uint8_t mifare_trailer_key_permissions(const uint8_t trailer[16], uint8_t key);
bool mifare_trailer_access_bits_valid(const uint8_t trailer[MIFARE_BLOCK_SIZE]);
uint8_t mifare_trailer_permissions(const mifare_access_t *access,
                                   uint8_t block);
bool mifare_authenticated_for_block(const mifare_access_t *access,
                                    uint8_t block);
/* Pure permission check on the target trailer, including Key B usability. */
bool mifare_data_permission(const uint8_t trailer[16],
                            uint8_t block,
                            uint8_t key,
                            uint8_t operation);
bool mifare_data_operation_allowed(const mifare_access_t *access,
                                   uint8_t block,
                                   uint8_t operation);

static inline bool mifare_can_read_block(const mifare_access_t *access,
                                         uint8_t block)
{
    if (block < access->first_block || block > access->last_block)
    {
        return false;
    }
    return ((access->read_allowed_mask >> (block - access->first_block)) &
            1u) != 0u;
}
