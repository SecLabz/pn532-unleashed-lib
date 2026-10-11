#pragma once

#include "cards/desfire/desfire.h"
#include "common/compiler.h"

#define DESFIRE_MAX_RESPONSE_DATA_BYTES 58u
#define ISO7816_CLA_DESFIRE 0x90u
#define ISO7816_SW1_DESFIRE 0x91u

#define ISODEP_MIN_NATIVE_FRAME_LEN 4u
#define ISODEP_MIN_WRAPPED_FRAME_LEN 8u
#define ISODEP_WRAPPED_DATA_OVERHEAD_BYTES 9u /* PCB, APDU/Lc/Le and CRC. */
enum
{
    DESFIRE_NATIVE_COMMAND_BYTE = 1,
    DESFIRE_NATIVE_PARAMS_BYTE,
    DESFIRE_WRAPPED_COMMAND_BYTE = 2,
    DESFIRE_WRAPPED_P1_BYTE,
    DESFIRE_WRAPPED_P2_BYTE,
    DESFIRE_WRAPPED_LC_BYTE,
    DESFIRE_WRAPPED_PARAMS_BYTE,
};

#define DESFIRE_VERSION_NONE 0u
#define DESFIRE_VERSION_SOFTWARE_PENDING 1u
#define DESFIRE_VERSION_MANUFACTURING_PENDING 2u
#define DESFIRE_FILE_STANDARD 0x00u
#define DESFIRE_FILE_BACKUP 0x01u
#define DESFIRE_FILE_VALUE 0x02u
#define DESFIRE_FILE_LINEAR_RECORD 0x03u
#define DESFIRE_FILE_CYCLIC_RECORD 0x04u
#define DESFIRE_COMMUNICATION_PLAIN 0x00u
#define DESFIRE_ACCESS_FREE 0x0eu
#define DESFIRE_VALUE_BYTES 4u
#define DESFIRE_DATA_SETTINGS_BYTES 7u
#define DESFIRE_RECORD_SETTINGS_BYTES 13u
#define DESFIRE_FILE_READ_PARAMS_BYTES 7u
#define DESFIRE_FILE_READ_OFFSET_BYTE 1u
#define DESFIRE_FILE_READ_COUNT_BYTE 4u
enum
{
    DESFIRE_SETTINGS_TYPE_BYTE,
    DESFIRE_SETTINGS_COMMUNICATION_BYTE,
    DESFIRE_SETTINGS_READ_WRITE_BYTE,
    DESFIRE_SETTINGS_READ_BYTE,
    DESFIRE_SETTINGS_GEOMETRY_BYTE,
    DESFIRE_SETTINGS_CURRENT_RECORDS_BYTE = 10,
};

typedef struct
{
    uint8_t pcb;
    uint8_t command;
    const uint8_t *params;
    uint8_t params_len;
    bool wrapped;
} desfire_request_t;

typedef enum
{
    DESFIRE_COMMAND_GET_KEY_SETTINGS = 0x45,
    DESFIRE_COMMAND_SELECT_APPLICATION = 0x5a,
    DESFIRE_COMMAND_GET_VERSION = 0x60,
    DESFIRE_COMMAND_GET_KEY_VERSION = 0x64,
    DESFIRE_COMMAND_GET_APPLICATION_IDS = 0x6a,
    DESFIRE_COMMAND_GET_VALUE = 0x6c,
    DESFIRE_COMMAND_GET_FREE_MEMORY = 0x6e,
    DESFIRE_COMMAND_GET_FILE_IDS = 0x6f,
    DESFIRE_COMMAND_READ_RECORDS = 0xbb,
    DESFIRE_COMMAND_READ_DATA = 0xbd,
    DESFIRE_COMMAND_ADDITIONAL_FRAME = 0xaf,
    DESFIRE_COMMAND_GET_FILE_SETTINGS = 0xf5,
} desfire_command_t;

typedef enum
{
    DESFIRE_STATUS_OK = 0x00,
    DESFIRE_STATUS_ILLEGAL_COMMAND = 0x1c,
    DESFIRE_STATUS_LENGTH_ERROR = 0x7e,
    DESFIRE_STATUS_PERMISSION_DENIED = 0x9d,
    DESFIRE_STATUS_APPLICATION_NOT_FOUND = 0xa0,
    DESFIRE_STATUS_ADDITIONAL_FRAME = 0xaf,
    DESFIRE_STATUS_FILE_NOT_FOUND = 0xf0,
} desfire_status_t;

void desfire_emit_data(desfire_context_t *context,
                       const desfire_request_t *request,
                       const uint8_t *data,
                       uint32_t data_len,
                       uint8_t status,
                       nfca_tag_response_t *response);

void desfire_emit_status(desfire_context_t *context,
                         const desfire_request_t *request,
                         uint8_t status,
                         nfca_tag_response_t *response);

bool desfire_handle_capture(desfire_context_t *context,
                            const desfire_request_t *request,
                            nfca_tag_response_t *response);

desfire_app_t *desfire_current_app(desfire_context_t *context);
bool desfire_handle_file(desfire_context_t *context,
                         const desfire_request_t *request,
                         nfca_tag_response_t *response);
