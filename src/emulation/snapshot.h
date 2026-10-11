#pragma once
#include "pn532_unleashed/image.h"
void nfca_mifare_get_image(card_image_t *image);
void nfca_ntag_get_image(card_image_t *image);
void nfca_iso4_get_image(card_image_t *image);
bool nfcv_get_image(card_image_t *image);
