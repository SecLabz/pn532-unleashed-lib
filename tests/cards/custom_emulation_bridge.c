/* Exercise real engine dispatch and the example using decoded independent
 * requests. Analog modulation, PIO capture and response timing are not mocked
 * as successful RF evidence. */
#include "board/activity_led.h"
#include "emulation/emulation.h"
#include "phy/nfca/picc/nfca_rx.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "phy/nfcv/picc/nfcv_rx.h"
#include "phy/nfcv/picc/nfcv_tx.h"
#include "pn532/pn532_frontend.h"
#include "protocols/nfca/nfca_profile.h"
#include "protocols/nfcv/nfcv_profile.h"

#include <assert.h>
#include <string.h>
/* Reuse the standalone example without replacing this test entry point. */
#define main ntag_example_main
#include "../../examples/ntag_custom_handler/main.c"
#undef main

/* Startup calls belong to the example application, outside tested RF. */
bool set_sys_clock_khz(uint32_t frequency_khz, bool required)
{
    return frequency_khz != 0u && required;
}
bool stdio_init_all(void)
{
    return true;
}
nfc_status_t nfc_init(const nfc_config_t *config)
{
    (void)config;
    return NFC_OK;
}
nfc_status_t nfca_emulation_start(nfca_handler_t handler, void *context)
{
    return emulation_start_nfca(handler, context) ? NFC_OK : NFC_RF_ERROR;
}
nfc_status_t nfc_stop(void)
{
    return emulation_stop() ? NFC_OK : NFC_RF_ERROR;
}
void nfc_poll(void)
{
}

static nfca_rx_frame_handler_t nfca_rx;
static nfcv_rx_frame_handler_t nfcv_rx;
static nfca_response_t sent;
static uint32_t tx_count;
static uint32_t handler_count;
static uint8_t behavior;
static bool arm_failure;
static bool tx_failure;
#if PNU_ACTIVITY_LED
static uint32_t led_tx_count, led_rx_count;

void board_activity_led_record(bool transmitted, bool received)
{
    /* The notification must follow completed TX and must include silent RX. */
    assert(received);
    assert(tx_count == led_tx_count + (transmitted ? 1u : 0u));
    led_tx_count = tx_count;
    led_rx_count++;
}
#endif

bool pn532_frontend_stop(void)
{
    return true;
}
bool pn532_frontend_prepare_tag(uint8_t protocol)
{
    return protocol == 1u || protocol == 3u;
}
bool pn532_frontend_arm_tag(void)
{
    return !arm_failure;
}
void tag_board_init(void)
{
}
bool tag_phy_init(void)
{
    return true;
}
void tag_phy_stop(void)
{
}
void nfca_tx_set_fdt_base(uint32_t cycles)
{
    (void)cycles;
}
uint32_t time_us_32(void)
{
    return 123456u;
}
bool nfca_rx_init(nfca_rx_frame_handler_t handler)
{
    nfca_rx = handler;
    return true;
}
void nfca_rx_deinit(void)
{
    nfca_rx = NULL;
}
bool nfcv_rx_init(nfcv_rx_frame_handler_t handler)
{
    nfcv_rx = handler;
    return true;
}
void nfcv_rx_deinit(void)
{
    nfcv_rx = NULL;
}
bool nfcv_tx_init(void)
{
    return true;
}
bool nfcv_tx_send_response(uint32_t eof_us,
                           const uint8_t *data,
                           uint16_t size_bytes)
{
    assert(eof_us == 123456u && data != NULL);
    assert(size_bytes <= NFCV_TAG_MAX_RESPONSE_BYTES);
    if (tx_failure)
    {
        return false;
    }
    tx_count++;
    return true;
}
void nfca_tx_send_response(nfca_tag_response_t *response)
{
    sent = response->frame;
    tx_count++;
}

static bool builtin_nfca(const nfca_tag_request_t *request,
                         nfca_tag_response_t *response)
{
    (void)request;
    response->frame.type = NFCA_TAG_RESPONSE_DEFERRED_PARITY;
    response->frame.size_bytes = 1u;
    return true;
}
static bool builtin_nfcv(const nfcv_frame_t *request,
                         nfcv_tag_response_t *response)
{
    (void)request;
    nfcv_reply_ok(response);
    return true;
}
const nfca_tag_profile_t nfca_ntag_profile = {.handle_frame = builtin_nfca};
const nfca_tag_profile_t nfca_iso4_profile = {.handle_frame = builtin_nfca};
const nfcv_tag_profile_t nfcv_vicinity_profile = {.handle_frame = builtin_nfcv};
const nfca_tag_profile_t nfca_mifare_profile = {.handle_frame = builtin_nfca};
bool nfca_mifare_load_image(const card_image_t *image)
{
    return image != NULL;
}
bool nfca_ntag_load_image(const card_image_t *image)
{
    return image != NULL;
}
bool nfca_iso4_load_image(const card_image_t *image)
{
    return image != NULL;
}
bool nfcv_load_image(const card_image_t *image)
{
    return image != NULL;
}

static void custom_nfca(void *context,
                        const nfca_tag_request_t *request,
                        nfca_response_t *response)
{
    assert(context == &behavior && request != NULL);
    assert(response->type == NFCA_TAG_RESPONSE_NONE &&
           response->size_bytes == 0u);
    handler_count++;
    if (behavior == 1u)
    {
        nfca_response_short4(response, 0x0au);
    }
    else if (behavior == 2u)
    {
        response->type = NFCA_TAG_RESPONSE_DATA;
        response->size_bytes = NFCA_TAG_MAX_RESPONSE_BYTES + 1u;
    }
    else if (behavior == 3u)
    {
        response->type = NFCA_TAG_RESPONSE_DEFERRED_PARITY;
        response->size_bytes = 1u;
    }
}
static void custom_nfcv(void *context,
                        const nfcv_frame_t *request,
                        nfcv_tag_response_t *response)
{
    assert(context == &behavior && request != NULL &&
           response->size_bytes == 0u);
    handler_count++;
    if (behavior == 1u)
    {
        nfcv_reply_ok(response);
    }
    if (behavior == 2u)
    {
        response->size_bytes = NFCV_TAG_MAX_RESPONSE_BYTES + 1u;
    }
}

static void command(const uint8_t *data, uint16_t size_bytes, bool short_frame)
{
    nfca_frame_t frame = {.size_bytes = size_bytes,
                          .short_frame = short_frame,
                          .parity_ok = true,
                          .crc_ok = true};
    memcpy(frame.bytes, data, size_bytes);
#if PNU_ACTIVITY_LED
    const uint32_t before = led_rx_count;
#endif
    nfca_rx(&frame);
#if PNU_ACTIVITY_LED
    assert(led_rx_count == before + 1u);
#endif
}
static void select_demo(void)
{
    const uint8_t reqa[] = {0x52u};
    command(reqa, 1u, true);
    assert(sent.size_bytes == 2u && sent.bytes[0] == 0x44u);
    for (uint8_t level = 0u; level < 2u; level++)
    {
        const uint8_t anticoll[] = {(uint8_t)(0x93u + level * 2u), 0x20u};
        command(anticoll, 2u, false);
        assert(sent.size_bytes == 5u);
        uint8_t select[9] = {anticoll[0], 0x70u};
        memcpy(&select[2], sent.bytes, 5u);
        nfca_append_crc(select, 7u);
        command(select, sizeof(select), false);
        assert(sent.size_bytes == 3u &&
               nfca_check_crc(sent.bytes, sent.size_bytes));
    }
}

int main(void)
{
    assert(emulation_start_nfca(custom_nfca, &behavior));
    const uint8_t reqa[] = {0x26u};
    for (behavior = 0u; behavior < 4u; behavior++)
    {
        command(reqa, sizeof(reqa), true);
    }
    assert(handler_count == 4u && tx_count == 1u);
    assert(emulation_stop() && nfca_rx == NULL);
    assert(emulation_start_nfcv(custom_nfcv, &behavior));
    nfcv_frame_t frame = {0};
    for (behavior = 0u; behavior < 3u; behavior++)
    {
        nfcv_rx(&frame, 123456u);
    }
    assert(handler_count == 7u && tx_count == 2u);
    behavior = 1u;
    tx_failure = true;
    nfcv_rx(&frame, 123456u);
    assert(tx_count == 2u);
    tx_failure = false;
#if PNU_ACTIVITY_LED
    assert(led_rx_count == 8u && led_tx_count == 2u);
#endif
    assert(emulation_stop() && nfcv_rx == NULL);
    arm_failure = true;
    assert(!emulation_start_nfca(custom_nfca, &behavior));
    assert(nfca_rx == NULL);
    arm_failure = false;
    card_image_t classic = {.model = CARD_CLASSIC_1K};
    assert(emulation_load(&classic));
    command(reqa, sizeof(reqa), true);
    assert(sent.type == NFCA_TAG_RESPONSE_DEFERRED_PARITY);
    assert(emulation_stop());

    demo_ntag_t tag = demo_tag;
    assert(demo_ntag_init(&tag));
    const uint8_t uid[] = {4u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
    assert(memcmp(tag.activation.uid, uid, sizeof(uid)) == 0);
    /* Editing the table updates activation; inconsistent BCC is rejected. */
    tag.memory[0][0] ^= 1u;
    assert(!demo_ntag_init(&tag));
    tag.memory[0][3] ^= 1u;
    assert(demo_ntag_init(&tag) && tag.activation.uid[0] == 5u);
    tag.memory[1][0] ^= 1u;
    assert(!demo_ntag_init(&tag));
    tag.memory[2][0] ^= 1u;
    assert(demo_ntag_init(&tag) && tag.activation.uid[3] == 0x32u);
    tag = demo_tag;
    assert(demo_ntag_init(&tag));
    assert(emulation_start_nfca(demo_ntag_handle, &tag));
    select_demo();
    uint8_t read[] = {0x30u, 4u, 0u, 0u};
    nfca_append_crc(read, 2u);
    command(read, sizeof(read), false);
    assert(sent.size_bytes == 18u &&
           nfca_check_crc(sent.bytes, sent.size_bytes));
    for (uint8_t byte = 0u; byte < 16u; byte++)
    {
        assert(sent.bytes[byte] == 0x10u + byte);
    }
    /* Exercise every user page and the three READ wrap positions. */
    for (uint8_t page = 4u; page <= 36u; page++)
    {
        read[1] = page;
        nfca_append_crc(read, 2u);
        command(read, sizeof(read), false);
        assert(sent.size_bytes == 18u &&
               nfca_check_crc(sent.bytes, sent.size_bytes));
        for (uint8_t byte = 0u; byte < 16u; byte++)
        {
            assert(sent.bytes[byte] == page * 4u + byte);
        }
    }
    const uint8_t wrapped[] = {
        0x00u, 0x00u, 0x00u, 0x00u, /* Page 42 */
        0x00u, 0x00u, 0x00u, 0x00u, /* Page 43 */
        0x00u, 0x00u, 0x00u, 0x00u, /* Page 44 */
        0x04u, 0x11u, 0x22u, 0xbfu, /* Page  0 */
        0x33u, 0x44u, 0x55u, 0x66u, /* Page  1 */
        0x44u, 0x48u, 0xffu, 0xffu, /* Page  2 */
    };
    for (uint8_t page = 42u; page <= 44u; page++)
    {
        read[1] = page;
        nfca_append_crc(read, 2u);
        command(read, sizeof(read), false);
        assert(sent.size_bytes == 18u &&
               nfca_check_crc(sent.bytes, sent.size_bytes));
        assert(memcmp(sent.bytes, &wrapped[(page - 42u) * 4u], 16u) == 0);
    }
    read[1] = 45u;
    nfca_append_crc(read, 2u);
    command(read, sizeof(read), false);
    assert(sent.type == NFCA_TAG_RESPONSE_SHORT4 &&
           sent.short4 == NFCA_NACK_INVALID);
    uint8_t version[] = {0x60u, 0u, 0u};
    nfca_append_crc(version, 1u);
    command(version, sizeof(version), false);
    assert(sent.size_bytes == 10u && memcmp(sent.bytes, tag.version, 8u) == 0);
    /* Malformed lengths and unsupported writes produce a NACK. */
    uint8_t invalid[][8] = {
        {0x60u, 0x00u, 0x00u, 0x00u},
        {0x30u, 0x00u, 0x00u},
        {0xa2u, 0x04u, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u, 0x00u},
    };
    const uint8_t lengths[] = {4u, 3u, 8u};
    for (uint8_t index = 0u; index < 3u; index++)
    {
        nfca_append_crc(invalid[index], lengths[index] - 2u);
        command(invalid[index], lengths[index], false);
        assert(sent.type == NFCA_TAG_RESPONSE_SHORT4 &&
               sent.short4 == NFCA_NACK_INVALID);
    }
    assert(memcmp(tag.memory, demo_tag.memory, sizeof(tag.memory)) == 0);
    /* The callback must stay silent for empty or damaged requests. */
    nfca_tag_request_t damaged = {
        .bytes = version, .size_bytes = 0u, .parity_ok = true, .crc_ok = true};
    nfca_response_t ignored = {0};
    demo_ntag_handle(&tag, &damaged, &ignored);
    assert(ignored.type == NFCA_TAG_RESPONSE_NONE);
    damaged.size_bytes = sizeof(version);
    damaged.crc_ok = false;
    demo_ntag_handle(&tag, &damaged, &ignored);
    assert(ignored.type == NFCA_TAG_RESPONSE_NONE);
    damaged.crc_ok = true;
    damaged.parity_ok = false;
    demo_ntag_handle(&tag, &damaged, &ignored);
    assert(ignored.type == NFCA_TAG_RESPONSE_NONE);
    const uint8_t halt[] = {0x50u, 0u, 0x57u, 0xcdu};
    const uint32_t before = tx_count;
    command(halt, sizeof(halt), false);
    command(read, sizeof(read), false);
    assert(tx_count == before);
    assert(emulation_stop());
    return 0;
}
