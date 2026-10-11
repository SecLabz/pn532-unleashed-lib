#include "cards/image_copy.h"

#include "cards/image_internal.h"

#include <string.h>

typedef struct
{
    uint8_t *bytes;
    size_t used;
    bool copy;
} copy_t;
static void
blob(copy_t *copy, const uint8_t **output, const uint8_t *source, size_t size)
{
    if (copy->copy)
    {
        if (size != 0u)
        {
            memcpy(&copy->bytes[copy->used], source, size);
        }
        *output = size == 0u ? NULL : &copy->bytes[copy->used];
    }
    copy->used += size;
}
static void
sections(copy_t *copy, const card_image_t *source, card_image_t *out)
{
    blob(copy, &out->uid, source->uid, source->uid_size_bytes);
    blob(copy, &out->memory, source->memory, source->memory_size_bytes);
    if (!card_model_is_nfca(source->model))
    {
        blob(copy,
             &out->state.nfcv.locks,
             source->state.nfcv.locks,
             source->state.nfcv.block_count);
    }
    if (source->model != CARD_DESFIRE)
    {
        return;
    }
    const desfire_capture_t *capture = &source->state.iso4.capture;
    desfire_capture_t *dest = &out->state.iso4.capture;
    blob(copy,
         &dest->metadata.picc_key_settings,
         capture->metadata.picc_key_settings,
         capture->metadata.picc_key_settings_len);
    blob(copy,
         &dest->metadata.picc_key_versions,
         capture->metadata.picc_key_versions,
         capture->metadata.picc_key_versions_len);
    for (uint8_t i = 0u; i < capture->app_count; i++)
    {
        blob(copy,
             &dest->apps[i].key_settings,
             capture->apps[i].key_settings,
             capture->apps[i].key_settings_len);
        blob(copy,
             &dest->apps[i].key_versions,
             capture->apps[i].key_versions,
             capture->apps[i].key_versions_len);
    }
    for (uint8_t i = 0u; i < capture->file_count; i++)
    {
        blob(copy,
             &dest->files[i].settings,
             capture->files[i].settings,
             capture->files[i].settings_len);
        blob(copy,
             &dest->files[i].data,
             capture->files[i].data,
             capture->files[i].data_len);
    }
}
bool card_image_copy(const card_image_t *source,
                     card_image_t *output,
                     uint8_t *storage,
                     size_t capacity_bytes)
{
    copy_t copy = {0};
    sections(&copy, source, output);
    if (copy.used > capacity_bytes)
    {
        return false;
    }
    *output = *source;
    copy = (copy_t){.bytes = storage, .copy = true};
    sections(&copy, source, output);
    return true;
}
