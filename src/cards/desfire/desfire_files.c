#include "cards/desfire/desfire_internal.h"
#include "common/endian.h"

static desfire_file_t *RAM_FUNC(find_file)(desfire_context_t *context,
                                           uint8_t id)
{
    desfire_app_t *app = desfire_current_app(context);
    if (app == NULL)
    {
        return NULL;
    }
    for (uint8_t index = 0u; index < app->file_count; index++)
    {
        desfire_file_t *file = &context->capture.files[app->first_file + index];
        if (file->id == id)
        {
            return file;
        }
    }
    return NULL;
}

static bool RAM_FUNC(read_range)(const desfire_file_t *file,
                                 const desfire_request_t *request,
                                 const uint8_t **data,
                                 uint32_t *size_bytes)
{
    const bool read_records = request->command == DESFIRE_COMMAND_READ_RECORDS;
    uint32_t offset =
        read_le24(&request->params[DESFIRE_FILE_READ_OFFSET_BYTE]);
    uint32_t count = read_le24(&request->params[DESFIRE_FILE_READ_COUNT_BYTE]);
    uint32_t unit_bytes = 1u;
    uint32_t units = read_le24(&file->settings[DESFIRE_SETTINGS_GEOMETRY_BYTE]);
    if (read_records)
    {
        if (file->settings_len < DESFIRE_RECORD_SETTINGS_BYTES || units == 0u)
        {
            return false;
        }
        unit_bytes = units;
        units =
            read_le24(&file->settings[DESFIRE_SETTINGS_CURRENT_RECORDS_BYTE]);
    }
    /* Record offsets/counts use records; data offsets/counts use
     * bytes. Division bounds both conversions before multiplication. */
    if (units > file->data_len / unit_bytes || offset > units)
    {
        return false;
    }
    if (count == 0u || count > units - offset)
    {
        count = units - offset;
    }
    offset *= unit_bytes;
    *size_bytes = count * unit_bytes;
    *data = file->data + offset;

    return true;
}

static bool RAM_FUNC(handle_value)(desfire_context_t *context,
                                   const desfire_request_t *request,
                                   const desfire_file_t *file,
                                   nfca_tag_response_t *response)
{
    const uint8_t *data = file->data;
    const uint32_t size_bytes = file->data_len;
    if (size_bytes != DESFIRE_VALUE_BYTES)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }
    desfire_emit_data(
        context, request, data, size_bytes, DESFIRE_STATUS_OK, response);
    return true;
}

bool RAM_FUNC(desfire_handle_file)(desfire_context_t *context,
                                   const desfire_request_t *request,
                                   nfca_tag_response_t *response)
{
    const uint8_t command = request->command;
    const bool read_data = command == DESFIRE_COMMAND_READ_DATA;
    const bool read_records = command == DESFIRE_COMMAND_READ_RECORDS;
    if (!read_data && !read_records &&
        command != DESFIRE_COMMAND_GET_FILE_SETTINGS &&
        command != DESFIRE_COMMAND_GET_VALUE)
    {
        return false;
    }
    if (request->params_len !=
        (read_data || read_records ? DESFIRE_FILE_READ_PARAMS_BYTES : 1u))
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }
    desfire_file_t *file = find_file(context, request->params[0]);
    if (file == NULL)
    {
        const desfire_app_t *app = desfire_current_app(context);
        const uint8_t status =
            app != NULL && app->files_state != DESFIRE_CAPTURE_AVAILABLE
                ? DESFIRE_STATUS_PERMISSION_DENIED
                : DESFIRE_STATUS_FILE_NOT_FOUND;
        desfire_emit_status(context, request, status, response);
        return true;
    }
    if (file->settings_state != DESFIRE_CAPTURE_AVAILABLE ||
        (command != DESFIRE_COMMAND_GET_FILE_SETTINGS &&
         file->data_state != DESFIRE_CAPTURE_AVAILABLE))
    {
        const uint8_t status = context->capture.metadata.reports_unavailable
                                   ? DESFIRE_STATUS_PERMISSION_DENIED
                                   : DESFIRE_STATUS_FILE_NOT_FOUND;
        desfire_emit_status(context, request, status, response);
        return true;
    }
    if (command == DESFIRE_COMMAND_GET_FILE_SETTINGS)
    {
        desfire_emit_data(context,
                          request,
                          file->settings,
                          file->settings_len,
                          DESFIRE_STATUS_OK,
                          response);
        return true;
    }
    if (file->settings_len < DESFIRE_DATA_SETTINGS_BYTES)
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }
    /* No authenticated session or secure messaging is implemented.
     * Access-right nibbles R/RW must explicitly allow a public read.
     * Extended rights remain observable, but are not interpreted here. */
    if (file->settings[DESFIRE_SETTINGS_COMMUNICATION_BYTE] !=
            DESFIRE_COMMUNICATION_PLAIN ||
        ((file->settings[DESFIRE_SETTINGS_READ_WRITE_BYTE] >> 4) !=
             DESFIRE_ACCESS_FREE &&
         (file->settings[DESFIRE_SETTINGS_READ_BYTE] >> 4) !=
             DESFIRE_ACCESS_FREE))
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_PERMISSION_DENIED, response);
        return true;
    }
    const uint8_t type = file->settings[DESFIRE_SETTINGS_TYPE_BYTE];
    if ((read_data && type != DESFIRE_FILE_STANDARD &&
         type != DESFIRE_FILE_BACKUP) ||
        (read_records && type != DESFIRE_FILE_LINEAR_RECORD &&
         type != DESFIRE_FILE_CYCLIC_RECORD) ||
        (command == DESFIRE_COMMAND_GET_VALUE && type != DESFIRE_FILE_VALUE))
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_ILLEGAL_COMMAND, response);
        return true;
    }
    const uint8_t *data = file->data;
    uint32_t size_bytes = file->data_len;
    if (command == DESFIRE_COMMAND_GET_VALUE)
    {
        return handle_value(context, request, file, response);
    }
    if (!read_range(file, request, &data, &size_bytes))
    {
        desfire_emit_status(
            context, request, DESFIRE_STATUS_LENGTH_ERROR, response);
        return true;
    }

    desfire_emit_data(
        context, request, data, size_bytes, DESFIRE_STATUS_OK, response);
    return true;
}
