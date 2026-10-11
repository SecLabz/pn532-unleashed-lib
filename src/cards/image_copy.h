#pragma once
#include "pn532_unleashed/image.h"
/* Validated source; independent copy with borrowed pointers relocated to
 * caller byte storage. Output/storage unchanged if capacity is insufficient.
 * No overlap between source, output and storage. */
bool card_image_copy(const card_image_t *source,
                     card_image_t *output,
                     uint8_t *storage,
                     size_t capacity_bytes);
