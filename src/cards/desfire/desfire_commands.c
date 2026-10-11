#include "cards/desfire/desfire_internal.h"
#include "common/endian.h"
#include "protocols/nfca/nfca_profile.h"

#include <string.h>

static void RAM_FUNC(desfire_clear_pending)(desfire_context_t *context)
{
    context->pending_data = NULL;
    context->pending_len = 0u;
    context->pending_offset = 0u;
    context->pending_status = 0u;
}

void RAM_FUNC(desfire_reset_session)(desfire_context_t *context)
{
    context->response_data_bytes = DESFIRE_MAX_RESPONSE_DATA_BYTES;
    context->version_part = DESFIRE_VERSION_NONE;
    context->selected_app = -1;
    desfire_clear_pending(context);
}

static void RAM_FUNC(desfire_activate_pending)(desfire_context_t *context,
                                               const uint8_t *data,
                                               uint32_t len,
                                               uint8_t final_status)
{
    context->pending_data = data;
    context->pending_len = len;
    context->pending_offset = 0u;
    context->pending_status = final_status;
}

static void RAM_FUNC(desfire_emit_chunk)(desfire_context_t *context,
                                         const desfire_request_t *request,
                                         nfca_tag_response_t *response)
{
    response->frame.bytes[0] = request->pcb;
    uint32_t remaining = context->pending_len - context->pending_offset;
    uint8_t chunk = remaining > context->response_data_bytes
                        ? context->response_data_bytes
                        : (uint8_t)remaining;
    const bool more = remaining > chunk;
    const uint8_t status =
        more ? DESFIRE_STATUS_ADDITIONAL_FRAME : context->pending_status;
    if (request->wrapped)
    {
        if (chunk != 0u)
        {
            memcpy(&response->frame.bytes[1],
                   &context->pending_data[context->pending_offset],
                   chunk);
        }
        response->frame.bytes[1u + chunk] = ISO7816_SW1_DESFIRE;
        response->frame.bytes[2u + chunk] = status;
        nfca_tag_response_data_crc(response, (uint16_t)(3u + chunk));
    }
    else
    {
        response->frame.bytes[1] = status;
        if (chunk != 0u)
        {
            memcpy(&response->frame.bytes[2],
                   &context->pending_data[context->pending_offset],
                   chunk);
        }
        nfca_tag_response_data_crc(response, (uint16_t)(2u + chunk));
    }
    context->pending_offset += chunk;
    if (!more)
    {
        desfire_clear_pending(context);
    }
}

void RAM_FUNC(desfire_emit_status)(desfire_context_t *context,
                                   const desfire_request_t *request,
                                   uint8_t status,
                                   nfca_tag_response_t *response)
{
    desfire_emit_data(context, request, NULL, 0u, status, response);
}

static inline void RAM_FUNC(desfire_emit_ok)(desfire_context_t *context,
                                             const desfire_request_t *request,
                                             const uint8_t *data,
                                             uint32_t len,
                                             nfca_tag_response_t *response)
{
    desfire_emit_data(context, request, data, len, DESFIRE_STATUS_OK, response);
}

void RAM_FUNC(desfire_emit_data)(desfire_context_t *context,
                                 const desfire_request_t *request,
                                 const uint8_t *data,
                                 uint32_t data_len,
                                 uint8_t status,
                                 nfca_tag_response_t *response)
{
    desfire_activate_pending(context, data, data_len, status);
    desfire_emit_chunk(context, request, response);
}

desfire_app_t *RAM_FUNC(desfire_current_app)(desfire_context_t *context)
{
    if (context->selected_app < 0 ||
        (uint8_t)context->selected_app >= context->capture.app_count)
    {
        return NULL;
    }
    return &context->capture.apps[(uint8_t)context->selected_app];
}

static bool RAM_FUNC(handle_get_version)(desfire_context_t *context,
                                         const desfire_request_t *request,
                                         nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    context->version_part = DESFIRE_VERSION_SOFTWARE_PENDING;
    desfire_emit_data(context,
                      request,
                      metadata->version,
                      DESFIRE_VERSION_COMPONENT_BYTES,
                      DESFIRE_STATUS_ADDITIONAL_FRAME,
                      response);
    return true;
}

static bool RAM_FUNC(handle_get_free_memory)(desfire_context_t *context,
                                             const desfire_request_t *request,
                                             nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    if (!metadata->free_memory_present)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_ILLEGAL_COMMAND, response);
        return true;
    }
    write_le24(context->scratch, metadata->free_memory);
    desfire_emit_ok(
        context, request, context->scratch, DESFIRE_SIZE_BYTES, response);
    return true;
}

static bool RAM_FUNC(handle_get_key_settings)(desfire_context_t *context,
                                              const desfire_request_t *request,
                                              nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    desfire_app_t *app = desfire_current_app(context);
    const uint8_t *settings =
        app != NULL ? app->key_settings : metadata->picc_key_settings;
    const uint8_t settings_len =
        app != NULL ? app->key_settings_len : metadata->picc_key_settings_len;
    const uint8_t settings_state =
        app != NULL ? app->settings_state : metadata->picc_settings_state;
    if (settings_state != DESFIRE_CAPTURE_AVAILABLE || settings_len == 0u)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_PERMISSION_DENIED, response);
        return true;
    }
    desfire_emit_ok(context, request, settings, settings_len, response);
    return true;
}

static bool RAM_FUNC(handle_get_key_version)(desfire_context_t *context,
                                             const desfire_request_t *request,
                                             nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    const uint8_t *params = request->params;
    const uint8_t params_len = request->params_len;
    if (params_len < 1u)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }
    desfire_app_t *app = desfire_current_app(context);
    const uint8_t *versions =
        app != NULL ? app->key_versions : metadata->picc_key_versions;
    const uint8_t versions_len =
        app != NULL ? app->key_versions_len : metadata->picc_key_versions_len;
    if (params[0] >= versions_len)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_PERMISSION_DENIED, response);
        return true;
    }
    context->scratch[0] = versions[params[0]];
    desfire_emit_ok(context, request, context->scratch, 1u, response);
    return true;
}

static bool
RAM_FUNC(handle_get_application_ids)(desfire_context_t *context,
                                     const desfire_request_t *request,
                                     nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    if (metadata->applications_state != DESFIRE_CAPTURE_AVAILABLE)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_PERMISSION_DENIED, response);
        return true;
    }
    uint8_t offset = 0u;
    for (uint8_t index = 0u; index < context->capture.app_count; index++)
    {
        memcpy(&context->scratch[offset],
               context->capture.apps[index].aid,
               DESFIRE_AID_BYTES);
        offset += DESFIRE_AID_BYTES;
    }
    desfire_emit_ok(context, request, context->scratch, offset, response);
    return true;
}

static bool
RAM_FUNC(handle_select_application)(desfire_context_t *context,
                                    const desfire_request_t *request,
                                    nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    const uint8_t *params = request->params;
    const uint8_t params_len = request->params_len;
    if (params_len < DESFIRE_AID_BYTES)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }
    if (params[0] == 0u && params[1] == 0u && params[2] == 0u)
    {
        context->selected_app = -1;
        desfire_emit_status(context, request, DESFIRE_STATUS_OK, response);
        return true;
    }
    for (uint8_t index = 0u; index < context->capture.app_count; index++)
    {
        if (memcmp(params,
                   context->capture.apps[index].aid,
                   DESFIRE_AID_BYTES) == 0)
        {
            context->selected_app = (int8_t)index;
            desfire_emit_status(context, request, DESFIRE_STATUS_OK, response);
            return true;
        }
    }
    desfire_emit_status(context,
                        request,
                        metadata->reports_unavailable &&
                                metadata->applications_state !=
                                    DESFIRE_CAPTURE_AVAILABLE
                            ? DESFIRE_STATUS_PERMISSION_DENIED
                            : DESFIRE_STATUS_APPLICATION_NOT_FOUND,
                        response);
    return true;
}

static bool RAM_FUNC(handle_get_file_ids)(desfire_context_t *context,
                                          const desfire_request_t *request,
                                          nfca_tag_response_t *response)
{

    desfire_app_t *app = desfire_current_app(context);
    if (app == NULL)
    {
        desfire_emit_ok(context, request, NULL, 0u, response);
        return true;
    }
    if (app->files_state != DESFIRE_CAPTURE_AVAILABLE)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_PERMISSION_DENIED, response);
        return true;
    }
    for (uint8_t index = 0u; index < app->file_count; index++)
    {
        context->scratch[index] =
            context->capture.files[app->first_file + index].id;
    }
    desfire_emit_ok(
        context, request, context->scratch, app->file_count, response);
    return true;
}

static bool RAM_FUNC(handle_additional_frame)(desfire_context_t *context,
                                              const desfire_request_t *request,
                                              nfca_tag_response_t *response)
{
    const desfire_capture_metadata_t *metadata = &context->capture.metadata;
    if (context->version_part == DESFIRE_VERSION_SOFTWARE_PENDING)
    {
        context->version_part = DESFIRE_VERSION_MANUFACTURING_PENDING;
        desfire_emit_data(context,
                          request,
                          &metadata->version[DESFIRE_VERSION_COMPONENT_BYTES],
                          DESFIRE_VERSION_COMPONENT_BYTES,
                          DESFIRE_STATUS_ADDITIONAL_FRAME,
                          response);
        return true;
    }
    if (context->version_part == DESFIRE_VERSION_MANUFACTURING_PENDING)
    {
        context->version_part = DESFIRE_VERSION_NONE;
        desfire_emit_ok(context,
                        request,
                        &metadata->version[DESFIRE_VERSION_MANUFACTURING_BYTE],
                        DESFIRE_VERSION_MANUFACTURING_BYTES,
                        response);
        return true;
    }
    if (context->pending_data != NULL)
    {
        desfire_emit_chunk(context, request, response);
        return true;
    }
    desfire_emit_status(
        context, request, DESFIRE_STATUS_ILLEGAL_COMMAND, response);
    return true;
}

bool RAM_FUNC(desfire_handle_capture)(desfire_context_t *context,
                                      const desfire_request_t *request,
                                      nfca_tag_response_t *response)
{
    if (context == NULL || request == NULL || response == NULL)
    {
        return false;
    }

    const uint8_t command = request->command;
    const uint8_t params_len = request->params_len;
    uint8_t expected_len = 0u;
    if (command == DESFIRE_COMMAND_SELECT_APPLICATION)
    {
        expected_len = 3u;
    }
    else if (command == DESFIRE_COMMAND_GET_KEY_VERSION)
    {
        expected_len = 1u;
    }
    if ((command == DESFIRE_COMMAND_ADDITIONAL_FRAME ||
         command == DESFIRE_COMMAND_GET_VERSION ||
         command == DESFIRE_COMMAND_GET_FREE_MEMORY ||
         command == DESFIRE_COMMAND_GET_KEY_SETTINGS ||
         command == DESFIRE_COMMAND_GET_KEY_VERSION ||
         command == DESFIRE_COMMAND_GET_APPLICATION_IDS ||
         command == DESFIRE_COMMAND_SELECT_APPLICATION ||
         command == DESFIRE_COMMAND_GET_FILE_IDS) &&
        params_len != expected_len)
    {
        desfire_clear_pending(context);
        context->version_part = DESFIRE_VERSION_NONE;
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }

    if (command == DESFIRE_COMMAND_ADDITIONAL_FRAME)
    {
        return handle_additional_frame(context, request, response);
    }

    desfire_clear_pending(context);
    context->version_part = DESFIRE_VERSION_NONE;

    switch (command)
    {
    case DESFIRE_COMMAND_GET_VERSION:
        return handle_get_version(context, request, response);
    case DESFIRE_COMMAND_GET_FREE_MEMORY:
        return handle_get_free_memory(context, request, response);
    case DESFIRE_COMMAND_GET_KEY_SETTINGS:
        return handle_get_key_settings(context, request, response);
    case DESFIRE_COMMAND_GET_KEY_VERSION:
        return handle_get_key_version(context, request, response);
    case DESFIRE_COMMAND_GET_APPLICATION_IDS:
        return handle_get_application_ids(context, request, response);
    case DESFIRE_COMMAND_SELECT_APPLICATION:
        return handle_select_application(context, request, response);
    case DESFIRE_COMMAND_GET_FILE_IDS:
        return handle_get_file_ids(context, request, response);
    default:
        if (desfire_handle_file(context, request, response))
        {
            return true;
        }
        desfire_emit_status(
            context, request, DESFIRE_STATUS_ILLEGAL_COMMAND, response);
        return true;
    }
}
