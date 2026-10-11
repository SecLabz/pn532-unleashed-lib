/* API boundary substitutes for executing the published C recipes. */
#include "pn532_unleashed.h"
#include "pn532_unleashed/nfca_desfire.h"
#include "pn532_unleashed/nfca_mifare.h"
#include "pn532_unleashed/nfca_mifare_magic.h"
#include "pn532_unleashed/nfca_mifare_uid.h"
#include "pn532_unleashed/nfcv_reader.h"

static nfc_status_t acquisition;
static nfc_status_t operation;
static nfc_status_t cleanup;
static unsigned int stops;
static unsigned int operations;

void host_recipe_config(nfc_status_t start,
                        nfc_status_t step,
                        nfc_status_t stop)
{
    acquisition = start;
    operation = step;
    cleanup = stop;
    stops = 0u;
    operations = 0u;
}

unsigned int host_recipe_stops(void)
{
    return stops;
}

unsigned int host_recipe_operations(void)
{
    return operations;
}

static nfc_status_t step(void)
{
    operations++;
    return operation;
}

nfc_status_t nfc_stop(void)
{
    stops++;
    return cleanup;
}

nfc_status_t nfca_select(const nfca_tag_t *expected, card_model_t model)
{
    return acquisition;
}
nfc_status_t nfca_write_page(uint8_t page,
                             const uint8_t *password,
                             const uint8_t before[4],
                             const uint8_t after[4])
{
    return step();
}

nfc_status_t nfca_reader_start(void)
{
    return acquisition;
}

nfc_status_t nfcv_reader_start(void)
{
    return acquisition;
}

nfc_status_t nfca_mifare_read_sector(uint8_t sector,
                                     bool key_b,
                                     const uint8_t key[6],
                                     uint8_t *memory,
                                     uint16_t capacity_bytes,
                                     uint16_t *size_bytes)
{
    return step();
}

nfc_status_t desfire_activate(desfire_session_t *session)
{
    return step();
}

nfc_status_t desfire_exchange(desfire_session_t *session,
                              const uint8_t *command,
                              size_t command_size_bytes,
                              uint8_t *reply,
                              size_t capacity_bytes,
                              size_t *size_bytes,
                              uint8_t *native_status)
{
    return step();
}

nfc_status_t nfcv_reader_inventory(nfcv_identity_t *tag)
{
    return step();
}

nfc_status_t nfcv_reader_exchange(const uint8_t *command,
                                  size_t command_size_bytes,
                                  nfcv_reader_reply_t *reply,
                                  const reader_options_t *options)
{
    return step();
}

nfc_status_t nfca_read_all(const uint8_t *password, nfca_dump_t *dump)
{
    return step();
}

nfc_status_t nfca_scan(nfca_tag_t *tag)
{
    tag->uid_size_bytes = 4u;
    return acquisition;
}

nfc_status_t nfca_mifare_magic(const nfca_tag_t *expected,
                               const uint8_t key[MIFARE_KEY_BYTES],
                               bool key_b,
                               mifare_magic_result_t *result)
{
    return step();
}

nfc_status_t nfca_mifare_set_uid(const mifare_uid_request_t *request)
{
    return acquisition;
}

nfc_status_t emulation_start(const card_image_t *image)
{
    return acquisition;
}
