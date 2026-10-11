#pragma once

#include "cards/type2/ntag_config.h"
#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfcv.h"

#define VICINITY_GENERIC_BLOCK_COUNT 64u

static inline bool card_model_is_classic(card_model_t model)
{
    return model == CARD_CLASSIC_MINI || model == CARD_CLASSIC_1K ||
           model == CARD_CLASSIC_2K || model == CARD_CLASSIC_4K;
}

static inline bool card_model_is_iso4(card_model_t model)
{
    return model == CARD_ISO_DEP || model == CARD_ISO4 ||
           model == CARD_ST25TA || model == CARD_MIFARE_PLUS ||
           model == CARD_DESFIRE;
}

static inline bool card_model_is_nfca(card_model_t model)
{
    return card_model_is_classic(model) || ntag_model_config(model) != NULL ||
           card_model_is_iso4(model);
}
