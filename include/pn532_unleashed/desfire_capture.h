#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Bounded public observations. Blob sizes are bytes; nonempty blobs borrow
 * caller storage through validation/start. Unavailable states preserve the
 * supplied observation state (0 available, 1 denied, 2 absent, 3 not captured).
 */
#define DESFIRE_MAX_CAPTURE_APPS 28u
#define DESFIRE_MAX_CAPTURE_FILES 64u
#define DESFIRE_MAX_SCRATCH_BYTES 96u
#define DESFIRE_AID_BYTES 3u
#define DESFIRE_MAX_FILE_ID 31u
#define DESFIRE_KEY_SETTINGS_BYTES 2u
#define DESFIRE_MAX_KEY_VERSIONS 14u
#define DESFIRE_VERSION_COMPONENT_BYTES 7u
#define DESFIRE_VERSION_MANUFACTURING_BYTE                                     \
    (2u * DESFIRE_VERSION_COMPONENT_BYTES)
#define DESFIRE_VERSION_MANUFACTURING_BYTES 14u
#define DESFIRE_VERSION_BYTES                                                  \
    (DESFIRE_VERSION_MANUFACTURING_BYTE + DESFIRE_VERSION_MANUFACTURING_BYTES)
#define DESFIRE_CAPTURE_AVAILABLE 0u
#define DESFIRE_SIZE_BYTES 3u
#define DESFIRE_SIZE_MAX 0xffffffu

typedef struct
{
    uint8_t aid[DESFIRE_AID_BYTES];
    uint8_t settings_state;
    const uint8_t *key_settings;
    uint8_t key_settings_len;
    const uint8_t *key_versions;
    uint8_t key_versions_len;
    uint8_t files_state;
    uint8_t first_file;
    uint8_t file_count;
} desfire_app_t;

typedef struct
{
    uint8_t id;
    uint8_t settings_state;
    const uint8_t *settings;
    uint16_t settings_len;
    uint8_t data_state;
    const uint8_t *data;
    uint32_t data_len;
} desfire_file_t;

typedef struct
{
    bool reports_unavailable;
    uint8_t version[DESFIRE_VERSION_BYTES];
    bool free_memory_present;
    uint32_t free_memory;
    uint8_t picc_settings_state;
    const uint8_t *picc_key_settings;
    uint8_t picc_key_settings_len;
    const uint8_t *picc_key_versions;
    uint8_t picc_key_versions_len;
    uint8_t applications_state;
} desfire_capture_metadata_t;

typedef struct
{
    desfire_capture_metadata_t metadata;
    desfire_app_t apps[DESFIRE_MAX_CAPTURE_APPS];
    uint8_t app_count;
    desfire_file_t files[DESFIRE_MAX_CAPTURE_FILES];
    uint8_t file_count;
} desfire_capture_t;
