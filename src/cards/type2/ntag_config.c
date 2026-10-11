#include "cards/type2/ntag_config.h"

static const ntag_config_t configurations[] = {
    {.model = CARD_ULTRALIGHT, .page_count = 16u},
    {
        .model = CARD_ULTRALIGHT_EV1_48,
        .page_count = 20u,
        .config_page = 16u,
        .version = {0u, 4u, 3u, 1u, 1u, 0u, 0x0bu, 3u},
        .ev1 = true,
    },
    {
        .model = CARD_ULTRALIGHT_EV1_128,
        .page_count = 41u,
        .config_page = 37u,
        .dynamic_lock_page = 36u,
        .dynamic_lock_granularity = NTAG_DOUBLE_PAGE_LOCK_GROUP_PAGES,
        .dynamic_lock_mask = 0x03ffu,
        .dynamic_block_lock_mask = 0x1fu,
        .version = {0u, 4u, 3u, 1u, 1u, 0u, 0x0eu, 3u},
        .ev1 = true,
    },
    {
        .model = CARD_NTAG213,
        .page_count = 45u,
        .config_page = 41u,
        .dynamic_lock_page = 40u,
        .dynamic_lock_granularity = NTAG_DOUBLE_PAGE_LOCK_GROUP_PAGES,
        .dynamic_lock_mask = 0x0fffu,
        .dynamic_block_lock_mask = 0x3fu,
        .version = {0u, 4u, 4u, 2u, 1u, 0u, 0x0fu, 3u},
        .ev1 = true,
        .is_ntag21x = true,
    },
    {
        .model = CARD_NTAG215,
        .page_count = 135u,
        .config_page = 131u,
        .dynamic_lock_page = 130u,
        .dynamic_lock_granularity = NTAG21X_LOCK_GROUP_PAGES,
        .dynamic_lock_mask = 0x00ffu,
        .dynamic_block_lock_mask = 0x0fu,
        .version = {0u, 4u, 4u, 2u, 1u, 0u, 0x11u, 3u},
        .ev1 = true,
        .is_ntag21x = true,
    },
    {
        .model = CARD_NTAG216,
        .page_count = 231u,
        .config_page = 227u,
        .dynamic_lock_page = 226u,
        .dynamic_lock_granularity = NTAG21X_LOCK_GROUP_PAGES,
        .dynamic_lock_mask = 0x3fffu,
        .dynamic_block_lock_mask = 0x7fu,
        .version = {0u, 4u, 4u, 2u, 1u, 0u, 0x13u, 3u},
        .ev1 = true,
        .is_ntag21x = true,
    },
};

const ntag_config_t *ntag_model_config(card_model_t model)
{
    for (size_t index = 0u;
         index < sizeof(configurations) / sizeof(configurations[0]);
         index++)
    {
        if (configurations[index].model == model)
        {
            return &configurations[index];
        }
    }
    return NULL;
}
