#pragma once

#include "cards/mifare/mifare_access.h"
#include "pn532_unleashed/image.h"

#include <stdbool.h>
#include <stdint.h>

#define MIFARE_MAX_BLOCK_COUNT 256u

typedef struct
{
    card_model_t model;
    uint16_t block_count;
    uint8_t sector_count;
    uint8_t atqa[NFCA_ATQA_BYTES];
    uint8_t sak;
} mifare_product_config_t;

typedef struct
{
    const mifare_product_config_t *config;
    uint8_t uid[NFCA_UID_SINGLE_BYTES];
    uint8_t memory[MIFARE_MAX_BLOCK_COUNT][MIFARE_BLOCK_SIZE];
} mifare_product_t;

/* NULL for unsupported models; configurations have static lifetime. */
const mifare_product_config_t *mifare_model_config(card_model_t model);

void mifare_product_init(mifare_product_t *product,
                         const mifare_product_config_t *config);
bool mifare_product_load(mifare_product_t *product, const card_image_t *image);
