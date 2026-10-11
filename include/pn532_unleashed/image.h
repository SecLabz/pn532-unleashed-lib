#pragma once

#include "pn532_unleashed/desfire_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        CARD_CLASSIC_1K = 1,
        CARD_NTAG215 = 2,
        CARD_SLIX = 3,
        CARD_SLIX_L = 4,
        CARD_CLASSIC_MINI = 5,
        CARD_CLASSIC_2K = 6,
        CARD_CLASSIC_4K = 7,
        CARD_NTAG213 = 8,
        CARD_NTAG216 = 9,
        CARD_ULTRALIGHT = 10,
        CARD_ULTRALIGHT_EV1_48 = 11,
        CARD_ULTRALIGHT_EV1_128 = 12,
        CARD_ISO15693 = 13,
        CARD_ISO_DEP = 14,
        CARD_ISO4 = 15,
        CARD_ST25TA = 16,
        CARD_MIFARE_PLUS = 17,
        CARD_DESFIRE = 18,
    } card_model_t;

    typedef enum
    {
        IMAGE_OK = 0,
        IMAGE_INVALID_ARGUMENT,
        IMAGE_UNSUPPORTED_MODEL,
        IMAGE_MISSING_STATE,
        IMAGE_INVALID_UID_SIZE,
        IMAGE_INVALID_MEMORY_SIZE,
        IMAGE_INVALID_UID,
        IMAGE_INVALID_ACCESS_BITS,
        IMAGE_INVALID_STATE,
        IMAGE_INVALID_CAPTURE,
    } image_status_t;

#define TYPE2_VERSION_BYTES 8u
#define TYPE2_SIGNATURE_BYTES 32u
#define TYPE2_COUNTER_COUNT 3u
#define CARD_STORAGE_MAX_BYTES 8314u
#define CARD_ATS_MAX_BYTES 32u
    typedef struct
    {
        uint8_t version[TYPE2_VERSION_BYTES];
        uint8_t signature[TYPE2_SIGNATURE_BYTES];
        uint32_t counters[TYPE2_COUNTER_COUNT];
        uint8_t tearing[TYPE2_COUNTER_COUNT];
        uint32_t authentication_attempts;
    } type2_state_t;

    typedef struct
    {
        const uint8_t *locks;
        uint8_t block_count;
        uint8_t dsfid;
        uint8_t afi;
        uint8_t ic_ref;
        bool dsfid_locked;
        bool afi_locked;
        bool eas_active;
        bool eas_locked;
        bool privacy_active;
        bool eas_password_protected;
        bool afi_password_protected;
        uint8_t privacy_password[4];
        uint8_t destroy_password[4];
        uint8_t eas_afi_password[4];
    } nfcv_state_t;

    typedef struct
    {
        uint8_t atqa[2];
        uint8_t sak;
        uint8_t ats[CARD_ATS_MAX_BYTES];
        uint8_t ats_size_bytes;
        desfire_capture_t capture; /* Used only by CARD_DESFIRE. */
    } iso4_state_t;

    /* Complete typed card state.
     * UID, memory, locks and DESFire blobs borrow caller storage through start.
     * Start copies all state. Sizes are bytes. Plain Ultralight/Classic require
     * zero state; EV1/NTAG require complete Type 2 state, including hidden
     * counters. NFC-V passwords are four RF-order bytes. No missing state is
     * inferred. */
    typedef struct
    {
        card_model_t model;
        const uint8_t *uid;
        uint16_t uid_size_bytes;
        const uint8_t *memory;
        uint16_t memory_size_bytes;
        union
        {
            type2_state_t type2;
            nfcv_state_t nfcv;
            iso4_state_t iso4;
        } state;
    } card_image_t;

    /* Validate geometry, required state and supported observations. No state
     * changes. Return the first invalid field or content; NULL is
     * INVALID_ARGUMENT. */
    image_status_t image_validate(const card_image_t *image);

    /* Static English diagnostic; valid for every status, including unknown
     * values. */
    const char *image_status_string(image_status_t status);

#ifdef __cplusplus
}
#endif
