#include "cards/mifare/mifare_product.h"

#include <string.h>

static const mifare_product_config_t mifare_1k_config = {
    .model = CARD_CLASSIC_1K,
    .block_count = 64u,
    .sector_count = 16u,
    .atqa = {0x04u, 0x00u},
    .sak = 0x08u,
};

static const mifare_product_config_t mifare_mini_config = {
    .model = CARD_CLASSIC_MINI,
    .block_count = 20u,
    .sector_count = 5u,
    .atqa = {0x04u, 0x00u},
    .sak = 0x09u,
};

static const mifare_product_config_t mifare_2k_config = {
    .model = CARD_CLASSIC_2K,
    .block_count = 128u,
    .sector_count = 32u,
    .atqa = {0x04u, 0x00u},
    .sak = 0x08u,
};

static const mifare_product_config_t mifare_4k_config = {
    .model = CARD_CLASSIC_4K,
    .block_count = 256u,
    .sector_count = 40u,
    .atqa = {0x02u, 0x00u},
    .sak = 0x18u,
};

const mifare_product_config_t *mifare_model_config(card_model_t model)
{
    switch (model)
    {
    case CARD_CLASSIC_MINI:
        return &mifare_mini_config;
    case CARD_CLASSIC_1K:
        return &mifare_1k_config;
    case CARD_CLASSIC_2K:
        return &mifare_2k_config;
    case CARD_CLASSIC_4K:
        return &mifare_4k_config;
    default:
        return NULL;
    }
}

void mifare_product_init(mifare_product_t *product,
                         const mifare_product_config_t *config)
{
    /* A validated image supplies all identity and memory before RF starts. */
    memset(product, 0, sizeof(*product));
    product->config = config;
}

bool mifare_product_load(mifare_product_t *product, const card_image_t *image)
{
    if (image == NULL || product->config == NULL ||
        image->model != product->config->model ||
        image->uid_size_bytes != sizeof(product->uid) ||
        image->memory_size_bytes !=
            (uint32_t)product->config->block_count * MIFARE_BLOCK_SIZE)
    {
        return false;
    }
    memcpy(product->uid, image->uid, sizeof(product->uid));
    memcpy(product->memory, image->memory, image->memory_size_bytes);
    return true;
}
