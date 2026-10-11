#include "cards/desfire/desfire.h"
#include "common/endian.h"

#include <string.h>

static bool keys_valid(uint8_t state,
                       uint8_t settings_len,
                       const uint8_t *settings,
                       uint8_t versions_len)
{
    if (state > 3u ||
        (state != 0u && (settings_len != 0u || versions_len != 0u)))
    {
        return false;
    }
    return state != 0u || (settings_len == 2u && (settings[1] & 0x0fu) <= 14u &&
                           versions_len <= (settings[1] & 0x0fu));
}

static bool file_valid(const desfire_file_t *file)
{
    if (file->settings_state > 3u || file->data_state > 3u ||
        (file->data_state != 0u && file->data_len != 0u))
    {
        return false;
    }
    if (file->settings_state != 0u)
    {
        return file->settings_len == 0u && file->data_state != 0u;
    }
    if (file->settings_len < 4u)
    {
        return false;
    }
    uint32_t expected;
    switch (file->settings[0])
    {
    case 0u:
    case 1u:
        if (file->settings_len < 7u)
        {
            return false;
        }
        expected = read_le24(&file->settings[4]);
        break;
    case 2u:
        if (file->settings_len < 17u || file->settings[16] > 1u)
        {
            return false;
        }
        expected = 4u;
        break;
    case 3u:
    case 4u:
    {
        if (file->settings_len < 13u)
        {
            return false;
        }
        const uint32_t width = read_le24(&file->settings[4]);
        const uint32_t maximum = read_le24(&file->settings[7]);
        const uint32_t count = read_le24(&file->settings[10]);
        if (width == 0u || count > maximum ||
            (count != 0u && width > UINT32_MAX / count))
        {
            return false;
        }
        expected = width * count;
        break;
    }
    default:
        return file->data_state != 0u;
    }
    if (file->data_state != 0u)
    {
        return true;
    }
    return expected == file->data_len && file->settings[1] == 0u &&
           ((file->settings[2] >> 4) == 0x0eu ||
            (file->settings[3] >> 4) == 0x0eu);
}

static bool blob_valid(const uint8_t *bytes, uint32_t size, uint32_t maximum)
{
    return size <= maximum && (size == 0u || bytes != NULL);
}
bool desfire_capture_valid(const desfire_capture_t *capture)
{
    if (capture == NULL || capture->app_count > DESFIRE_MAX_CAPTURE_APPS ||
        capture->file_count > DESFIRE_MAX_CAPTURE_FILES)
    {
        return false;
    }
    const desfire_capture_metadata_t *meta = &capture->metadata;
    if (meta->free_memory > DESFIRE_SIZE_MAX ||
        !blob_valid(meta->picc_key_settings,
                    meta->picc_key_settings_len,
                    DESFIRE_KEY_SETTINGS_BYTES) ||
        !blob_valid(meta->picc_key_versions,
                    meta->picc_key_versions_len,
                    DESFIRE_MAX_KEY_VERSIONS))
    {
        return false;
    }
    if (meta->reports_unavailable &&
        (!keys_valid(meta->picc_settings_state,
                     meta->picc_key_settings_len,
                     meta->picc_key_settings,
                     meta->picc_key_versions_len) ||
         meta->applications_state > 3u ||
         (meta->applications_state != 0u && capture->app_count != 0u) ||
         (!meta->free_memory_present && meta->free_memory != 0u)))
    {
        return false;
    }
    uint32_t total = meta->picc_key_settings_len + meta->picc_key_versions_len;
    uint8_t file_count = 0u;
    for (uint8_t index = 0u; index < capture->app_count; index++)
    {
        const desfire_app_t *app = &capture->apps[index];
        if ((app->aid[0] | app->aid[1] | app->aid[2]) == 0u ||
            app->first_file != file_count || file_count > capture->file_count ||
            app->file_count > capture->file_count - file_count ||
            !blob_valid(app->key_settings,
                        app->key_settings_len,
                        DESFIRE_KEY_SETTINGS_BYTES) ||
            !blob_valid(app->key_versions,
                        app->key_versions_len,
                        DESFIRE_MAX_KEY_VERSIONS))
        {
            return false;
        }
        for (uint8_t previous = 0u; previous < index; previous++)
        {
            if (memcmp(capture->apps[previous].aid,
                       app->aid,
                       DESFIRE_AID_BYTES) == 0)
            {
                return false;
            }
        }
        if (meta->reports_unavailable &&
            (!keys_valid(app->settings_state,
                         app->key_settings_len,
                         app->key_settings,
                         app->key_versions_len) ||
             app->files_state > 3u ||
             (app->files_state != 0u && app->file_count != 0u) ||
             app->file_count > 32u))
        {
            return false;
        }
        total += app->key_settings_len + app->key_versions_len;
        uint32_t ids = 0u;
        for (uint8_t number = 0u; number < app->file_count; number++)
        {
            const desfire_file_t *file = &capture->files[file_count++];
            if (file->id > DESFIRE_MAX_FILE_ID ||
                (ids & (1u << file->id)) != 0u ||
                !blob_valid(file->settings,
                            file->settings_len,
                            DESFIRE_MAX_SCRATCH_BYTES) ||
                !blob_valid(
                    file->data, file->data_len, CARD_STORAGE_MAX_BYTES) ||
                (meta->reports_unavailable && !file_valid(file)))
            {
                return false;
            }
            ids |= 1u << file->id;
            total += file->settings_len + file->data_len;
        }
    }
    return file_count == capture->file_count &&
           total <= CARD_STORAGE_MAX_BYTES - 10u;
}
