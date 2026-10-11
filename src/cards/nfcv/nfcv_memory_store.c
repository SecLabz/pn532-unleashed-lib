#include "pn532_unleashed/nfcv_memory_store.h"

#include <string.h>

bool nfcv_memory_store_init(nfcv_memory_store_t *store,
                            uint8_t block_size_bytes,
                            uint8_t block_count,
                            bool writable,
                            const uint8_t *initial_data,
                            uint16_t initial_data_size_bytes,
                            const uint8_t *initial_locks)
{
    if (store == NULL || initial_data == NULL || initial_locks == NULL ||
        block_size_bytes != NFCV_MEMORY_MAX_BLOCK_SIZE ||
        (block_count != SLIX_L_BLOCK_COUNT && block_count != SLIX_BLOCK_COUNT &&
         block_count != NFCV_MEMORY_MAX_BLOCKS) ||
        initial_data_size_bytes != (uint16_t)block_count * block_size_bytes)
    {
        return false;
    }
    for (uint8_t block = 0u; block < block_count; block++)
    {
        if (initial_locks[block] > 1u)
        {
            return false;
        }
    }
    memset(store, 0, sizeof(*store));
    store->block_size_bytes = block_size_bytes;
    store->block_count = block_count;
    store->writable = writable;
    memcpy(store->memory, initial_data, initial_data_size_bytes);
    memcpy(store->locks, initial_locks, block_count);
    return true;
}

void nfcv_memory_store_read(const nfcv_memory_store_t *store,
                            nfcv_tag_response_t *response,
                            bool option,
                            uint8_t first_block,
                            uint8_t block_count)
{
    if (first_block >= store->block_count || block_count == 0u ||
        first_block + block_count > store->block_count)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_UNAVAILABLE);
        return;
    }

    const uint16_t response_bytes =
        1u +
        (uint16_t)block_count * (store->block_size_bytes + (option ? 1u : 0u));
    if (response_bytes > sizeof(response->bytes) - NFCV_CRC_SIZE)
    {
        nfcv_reply_error(response, NFCV_ERR_GENERIC);
        return;
    }

    uint16_t out = 1u;
    response->bytes[0] = NFCV_RES_OK;
    for (uint8_t block = 0u; block < block_count; block++)
    {
        const uint8_t block_index = (uint8_t)(first_block + block);
        if (option)
        {
            response->bytes[out++] = store->locks[block_index];
        }
        const uint16_t memory_index =
            (uint16_t)block_index * store->block_size_bytes;
        memcpy(&response->bytes[out],
               &store->memory[memory_index],
               store->block_size_bytes);
        out = (uint16_t)(out + store->block_size_bytes);
    }
    response->size_bytes = out;
}

void nfcv_memory_store_write(nfcv_memory_store_t *store,
                             nfcv_tag_response_t *response,
                             const uint8_t *params,
                             uint8_t params_size_bytes)
{
    if (params_size_bytes != (uint8_t)(1u + store->block_size_bytes))
    {
        nfcv_reply_silent(response);
        return;
    }
    const uint8_t block = params[0];
    if (block >= store->block_count)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_UNAVAILABLE);
        return;
    }
    if (!store->writable || store->locks[block] != 0u)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_LOCKED);
        return;
    }
    memcpy(&store->memory[(uint16_t)block * store->block_size_bytes],
           &params[1],
           store->block_size_bytes);
    nfcv_reply_ok(response);
}

void nfcv_memory_store_lock(nfcv_memory_store_t *store,
                            nfcv_tag_response_t *response,
                            const uint8_t *params,
                            uint8_t params_size_bytes)
{
    if (params_size_bytes != 1u)
    {
        nfcv_reply_silent(response);
        return;
    }
    const uint8_t block = params[0];
    if (block >= store->block_count)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_UNAVAILABLE);
        return;
    }
    if (store->locks[block] != 0u)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_ALREADY_LOCKED);
        return;
    }
    store->locks[block] = 0x01u;
    nfcv_reply_ok(response);
}

void nfcv_memory_store_security(const nfcv_memory_store_t *store,
                                nfcv_tag_response_t *response,
                                const uint8_t *params,
                                uint8_t params_size_bytes)
{
    if (params_size_bytes != 2u)
    {
        nfcv_reply_silent(response);
        return;
    }
    const uint8_t first_block = params[0];
    const uint16_t count = (uint16_t)params[1] + 1u;
    if (first_block >= store->block_count ||
        first_block + count > store->block_count)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_UNAVAILABLE);
        return;
    }
    response->bytes[0] = NFCV_RES_OK;
    memcpy(&response->bytes[1], &store->locks[first_block], count);
    response->size_bytes = (uint16_t)(1u + count);
}
