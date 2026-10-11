#include "pn532_unleashed.h"
#include "pn532_unleashed/nfca_activation.h"
#include "pn532_unleashed/nfca_desfire.h"
#include "pn532_unleashed/nfca_mifare.h"
#include "pn532_unleashed/nfca_mifare_magic.h"
#include "pn532_unleashed/nfca_mifare_uid.h"
#include "pn532_unleashed/nfca_reader.h"
#include "pn532_unleashed/nfcv_reader.h"
#include "pn532_unleashed/nfcv_standard.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

static bool protocol_headers_link()
{
    const uint8_t uid[] = {0xdeu, 0xadu, 0xbeu, 0xefu};
    const uint8_t atqa[] = {4u, 0u};
    desfire_session_t desfire = {};
    uint8_t public_data[28] = {};
    size_t public_size = 0u;
    uint8_t native_status = 0u;
    (void)desfire_activate(&desfire);
    const uint8_t get_version = 0x60u;
    (void)desfire_exchange(&desfire,
                           &get_version,
                           1u,
                           public_data,
                           sizeof(public_data),
                           &public_size,
                           &native_status);
    (void)image_status_string(IMAGE_OK);
    nfca_activation_t activation;
    if (!nfca_activation_init(&activation, uid, sizeof(uid), atqa, 8u))
    {
        return false;
    }
    const uint8_t reqa = NFCA_CMD_REQA;
    nfca_tag_request_t request = {};
    request.bytes = &reqa;
    request.size_bytes = 1u;
    request.short_frame = true;
    nfca_response_t response;
    nfca_activation_event_t event;
    if (nfca_activation_handle(&activation, &request, &response, &event) !=
            NFCA_ACTIVATION_RESPONSE ||
        response.size_bytes != 2u)
    {
        return false;
    }
    const uint8_t vicinity_uid[] = {0xe0u, 4u, 3u, 1u, 2u, 3u, 4u, 5u};
    const uint8_t memory[32] = {};
    const uint8_t locks[8] = {};
    nfcv_memory_store_t store;
    nfcv_standard_t standard;
    if (!nfcv_memory_store_init(
            &store, 4u, 8u, true, memory, sizeof(memory), locks) ||
        !nfcv_standard_init(
            &standard, &store, vicinity_uid, 0u, 0u, 0u, false, false))
    {
        return false;
    }
    nfcv_frame_t frame = {};
    frame.bytes[0] = 0x26u;
    frame.bytes[1] = NFCV_CMD_INVENTORY;
    nfcv_append_crc(frame.bytes, 3u);
    frame.size_bytes = 5u;
    frame.crc_ok = nfcv_check_crc(frame.bytes, frame.size_bytes);
    nfcv_request_t vicinity_request;
    nfcv_tag_response_t vicinity_response;
    return nfcv_standard_prepare(&standard, &frame, &vicinity_request) &&
           nfcv_standard_handle(&standard,
                                &vicinity_request,
                                &vicinity_response) == NFCV_STANDARD_RESPONSE;
}

static void custom_nfca(void *, const nfca_tag_request_t *, nfca_response_t *)
{
}
static void custom_nfcv(void *, const nfcv_frame_t *, nfcv_tag_response_t *)
{
}

/* Link every application entry point from C++ without private headers or USB.
 * This is a build check, not an RF hardware test or a flashed application. */
int main()
{
    if (!protocol_headers_link())
    {
        return 4;
    }
    static card_image_t image = {};
    uint8_t snapshot[16] = {};
    (void)emulation_snapshot(&image, snapshot, sizeof(snapshot));
    if (image_validate(&image) == IMAGE_OK)
    {
        return 1;
    }
    nfc_config_t config = nfc_config_default();
    config.transport = PNU_CONSUMER_TRANSPORT;
    if (!set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true) ||
        nfc_init(&config) != NFC_OK)
    {
        return 2;
    }
    nfca_tag_t nfca_tag;
    static nfca_dump_t nfca_dump;
    (void)nfca_scan(&nfca_tag);
    (void)nfc_stop();
    mifare_magic_result_t magic = {};
    (void)nfca_mifare_magic(&nfca_tag, nullptr, false, &magic);
    mifare_uid_request_t uid_request = {};
    (void)nfca_mifare_uid_probe(&uid_request);
    (void)nfca_mifare_set_uid(&uid_request);
    (void)nfca_select(&nfca_tag, CARD_NTAG215);
    (void)nfca_read_all(nullptr, &nfca_dump);
    (void)nfc_stop();
    mifare_source_t source = {};
    mifare_observation_t observation;
    darkside_observation_t darkside;
    uint32_t nonce;
    bool writable;
    uint16_t size_bytes;
    const uint8_t keys[1][MIFARE_KEY_BYTES] = {};
    (void)nfca_select(&nfca_tag, CARD_CLASSIC_1K);
    (void)nfca_mifare_probe(&nonce);
    (void)nfca_mifare_check_key(0u, false, keys[0]);
    (void)nfca_mifare_read_sector(0u,
                                  false,
                                  keys[0],
                                  nfca_dump.memory,
                                  sizeof(nfca_dump.memory),
                                  &size_bytes);
    (void)nfca_mifare_read_block(
        0u, false, keys[0], nfca_dump.memory, &writable);
    (void)nfca_mifare_collect(&source, 4u, false, true, &observation);
    (void)nfca_mifare_darkside_start(0u, false, 0u);
    (void)nfca_mifare_darkside_step(&darkside);
    (void)nfc_stop();
    nfcv_tag_t tag;
    nfcv_slix_dump_t dump;
    (void)nfcv_scan(&tag);
    (void)nfc_stop();
    (void)nfcv_slix_read(&dump);
    (void)nfc_stop();
    uint8_t response[NFCA_READER_RX_MAX_BYTES];
    const uint8_t command[] = {NTAG_CMD_READ, 0u};
    nfca_reader_reply_t reply = {};
    reply.data = response;
    reply.capacity_bytes = sizeof(response);
    nfca_tx_frame_t tx = {};
    tx.data = command;
    tx.size_bits = 16u;
    nfca_rx_frame_t rx = {};
    rx.data = response;
    rx.capacity_bytes = sizeof(response);
    (void)nfca_reader_start();
    (void)nfca_reader_select(&nfca_tag);
    (void)nfca_reader_exchange(command, sizeof(command), &reply, nullptr);
    (void)nfca_reader_exchange_frame(&tx, &rx, nullptr);
    (void)nfc_stop();
    nfcv_identity_t identity;
    nfcv_reader_reply_t vicinity_reply = {};
    vicinity_reply.data = response;
    vicinity_reply.capacity_bytes = sizeof(response);
    nfcv_tx_frame_t vicinity_tx = {command, sizeof(command)};
    nfcv_rx_frame_t vicinity_rx = {};
    vicinity_rx.data = response;
    vicinity_rx.capacity_bytes = sizeof(response);
    (void)nfcv_reader_start();
    (void)nfcv_reader_inventory(&identity);
    (void)nfcv_reader_exchange(
        command, sizeof(command), &vicinity_reply, nullptr);
    (void)nfcv_reader_exchange_frame(&vicinity_tx, &vicinity_rx, nullptr);
    (void)nfc_stop();
    (void)nfca_emulation_start(custom_nfca, nullptr);
    (void)nfc_stop();
    (void)nfcv_emulation_start(custom_nfcv, nullptr);
    (void)nfc_stop();
    if (emulation_start(&image) != NFC_INVALID_IMAGE ||
        nfc_state() != NFC_STATE_IDLE)
    {
        return 3;
    }
    while (true)
    {
        nfc_poll();
        tight_loop_contents();
    }
}
