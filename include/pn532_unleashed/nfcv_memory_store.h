#pragma once

#include "pn532_unleashed/nfcv_tag.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NFCV_MEMORY_MAX_BLOCKS 64u
#define NFCV_MEMORY_MAX_BLOCK_SIZE 4u
#define NFCV_MEMORY_MAX_BYTES                                                  \
    (NFCV_MEMORY_MAX_BLOCKS * NFCV_MEMORY_MAX_BLOCK_SIZE)

    typedef struct
    {
        uint8_t block_size_bytes;
        uint8_t block_count;
        bool writable;
        uint8_t memory[NFCV_MEMORY_MAX_BYTES];
        uint8_t locks[NFCV_MEMORY_MAX_BLOCKS];
    } nfcv_memory_store_t;

    /* Own a copy of complete 4-byte blocks: 8 for SLIX-L, 28 for SLIX,
     * or 64 for generic ISO15693.
     * initial_data_size_bytes must match the geometry; initial_locks has
     * block_count bytes, each 0 or 1. Invalid/missing input leaves store
     * unchanged. Input ranges must not overlap store. No memory or locks are
     * invented. */
    bool nfcv_memory_store_init(nfcv_memory_store_t *store,
                                uint8_t block_size_bytes,
                                uint8_t block_count,
                                bool writable,
                                const uint8_t *initial_data,
                                uint16_t initial_data_size_bytes,
                                const uint8_t *initial_locks);

    /* Below: require an initialized non-NULL store and non-NULL response.
     * params is readable for params_size_bytes bytes; NULL is valid only at
     * size zero. These functions model emulated memory and never write a
     * physical tag. Malformed lengths yield silence; bounds/lock errors yield
     * protocol errors. Successful writes/locks update this caller-owned RAM
     * only. */
    void nfcv_memory_store_read(const nfcv_memory_store_t *store,
                                nfcv_tag_response_t *response,
                                bool option,
                                uint8_t first_block,
                                uint8_t block_count);
    void nfcv_memory_store_write(nfcv_memory_store_t *store,
                                 nfcv_tag_response_t *response,
                                 const uint8_t *params,
                                 uint8_t params_size_bytes);
    void nfcv_memory_store_lock(nfcv_memory_store_t *store,
                                nfcv_tag_response_t *response,
                                const uint8_t *params,
                                uint8_t params_size_bytes);
    void nfcv_memory_store_security(const nfcv_memory_store_t *store,
                                    nfcv_tag_response_t *response,
                                    const uint8_t *params,
                                    uint8_t params_size_bytes);

#ifdef __cplusplus
}
#endif
