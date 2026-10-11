#pragma once

#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfca_frame.h"

#define ISO4_MAX_ATS_BYTES 32u
#define ISO4_MIN_ATS_BYTES 2u /* TL and T0. */
#define ISO4_T0_TA_PRESENT 0x10u
#define ISO4_T0_TB_PRESENT 0x20u
#define ISO4_T0_TC_PRESENT 0x40u
#define ISO4_T0_RESERVED 0x80u
#define ISO4_T0_FSCI_MASK 0x0fu
#define ISO4_MAX_FSCI 8u

/* Validate UID size, wire ATQA, final SAK and ATS including TL, without CRC.
 * Caller supplies complete borrowed ATQA/ATS ranges; no RF state changes. */
bool iso4_activation_valid(uint16_t uid_size_bytes,
                           const uint8_t atqa[NFCA_ATQA_BYTES],
                           uint8_t sak,
                           const uint8_t *ats,
                           uint8_t ats_size_bytes);

bool iso4_image_valid(const card_image_t *image);
