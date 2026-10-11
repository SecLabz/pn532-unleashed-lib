#include "pn532_unleashed.h"
#include "pn532_unleashed/nfca_activation.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Caller-owned state, borrowed by the RF handler until nfc_stop(). */
typedef struct
{
    nfca_activation_t activation;
    uint8_t memory[45][4];
    uint8_t version[8];
} demo_ntag_t;

/* Edit one four-byte page per row, as in the Classic example. Keep both
 * BCC bytes consistent with the UID in pages 0 and 1. The handler rejects all
 * writes; these lock/configuration bytes do not model authentic NTAG state.
 * The context stays in RAM and is borrowed until nfc_stop(). */
// clang-format off
static demo_ntag_t demo_tag =
{
    .memory =
    {
        /* Identity and read-only Type 2 capability container. */
        {0x04, 0x11, 0x22, 0xbf}, /* Page  0: UID / BCC0 */
        {0x33, 0x44, 0x55, 0x66}, /* Page  1: UID */
        {0x44, 0x48, 0xff, 0xff}, /* Page  2: BCC1 / internal / lock */
        {0xe1, 0x10, 0x12, 0x0f}, /* Page  3: capability container */
        /* User data: raw byte-offset pattern, not an NDEF message. */
        {0x10, 0x11, 0x12, 0x13}, /* Page  4 */
        {0x14, 0x15, 0x16, 0x17}, /* Page  5 */
        {0x18, 0x19, 0x1a, 0x1b}, /* Page  6 */
        {0x1c, 0x1d, 0x1e, 0x1f}, /* Page  7 */
        {0x20, 0x21, 0x22, 0x23}, /* Page  8 */
        {0x24, 0x25, 0x26, 0x27}, /* Page  9 */
        {0x28, 0x29, 0x2a, 0x2b}, /* Page 10 */
        {0x2c, 0x2d, 0x2e, 0x2f}, /* Page 11 */
        {0x30, 0x31, 0x32, 0x33}, /* Page 12 */
        {0x34, 0x35, 0x36, 0x37}, /* Page 13 */
        {0x38, 0x39, 0x3a, 0x3b}, /* Page 14 */
        {0x3c, 0x3d, 0x3e, 0x3f}, /* Page 15 */
        {0x40, 0x41, 0x42, 0x43}, /* Page 16 */
        {0x44, 0x45, 0x46, 0x47}, /* Page 17 */
        {0x48, 0x49, 0x4a, 0x4b}, /* Page 18 */
        {0x4c, 0x4d, 0x4e, 0x4f}, /* Page 19 */
        {0x50, 0x51, 0x52, 0x53}, /* Page 20 */
        {0x54, 0x55, 0x56, 0x57}, /* Page 21 */
        {0x58, 0x59, 0x5a, 0x5b}, /* Page 22 */
        {0x5c, 0x5d, 0x5e, 0x5f}, /* Page 23 */
        {0x60, 0x61, 0x62, 0x63}, /* Page 24 */
        {0x64, 0x65, 0x66, 0x67}, /* Page 25 */
        {0x68, 0x69, 0x6a, 0x6b}, /* Page 26 */
        {0x6c, 0x6d, 0x6e, 0x6f}, /* Page 27 */
        {0x70, 0x71, 0x72, 0x73}, /* Page 28 */
        {0x74, 0x75, 0x76, 0x77}, /* Page 29 */
        {0x78, 0x79, 0x7a, 0x7b}, /* Page 30 */
        {0x7c, 0x7d, 0x7e, 0x7f}, /* Page 31 */
        {0x80, 0x81, 0x82, 0x83}, /* Page 32 */
        {0x84, 0x85, 0x86, 0x87}, /* Page 33 */
        {0x88, 0x89, 0x8a, 0x8b}, /* Page 34 */
        {0x8c, 0x8d, 0x8e, 0x8f}, /* Page 35 */
        {0x90, 0x91, 0x92, 0x93}, /* Page 36 */
        {0x94, 0x95, 0x96, 0x97}, /* Page 37 */
        {0x98, 0x99, 0x9a, 0x9b}, /* Page 38 */
        {0x9c, 0x9d, 0x9e, 0x9f}, /* Page 39 */
        /* Lock and configuration bytes (not interpreted by this handler). */
        {0x00, 0x00, 0x00, 0x00}, /* Page 40: dynamic lock */
        {0x00, 0x00, 0x00, 0xff}, /* Page 41: CFG0 / AUTH0 */
        {0x00, 0x00, 0x00, 0x00}, /* Page 42: CFG1 */
        {0x00, 0x00, 0x00, 0x00}, /* Page 43: password */
        {0x00, 0x00, 0x00, 0x00}, /* Page 44: PACK / RFU */
    },
    .version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0f, 0x03},
};
// clang-format on

static bool demo_ntag_init(demo_ntag_t *tag)
{
    uint8_t uid[7];
    memcpy(uid, tag->memory[0], 3u);
    memcpy(&uid[3], tag->memory[1], 4u);
    if (tag->memory[0][3] != nfca_bcc(0x88u, uid[0], uid[1], uid[2]) ||
        tag->memory[2][0] != nfca_bcc(uid[3], uid[4], uid[5], uid[6]))
    {
        return false;
    }
    const uint8_t atqa[] = {0x44u, 0x00u};
    return nfca_activation_init(&tag->activation, uid, sizeof(uid), atqa, 0u);
}

static void
__not_in_flash_func(demo_ntag_command)(const demo_ntag_t *tag,
                                       const nfca_tag_request_t *request,
                                       nfca_response_t *response)
{
    /* Request lengths include the two CRC-A bytes supplied by the decoder. */
    switch (request->bytes[0])
    {
    case NTAG_CMD_GET_VERSION:
        if (request->size_bytes != 3u)
        {
            break;
        }
        (void)nfca_response_data_crc(
            response, tag->version, sizeof(tag->version));
        return;

    case NTAG_CMD_READ:
    {
        if (request->size_bytes != 4u)
        {
            break;
        }
        const uint8_t start_page = request->bytes[1];
        const uint8_t page_count = sizeof(tag->memory) / sizeof(tag->memory[0]);
        if (start_page >= page_count)
        {
            break;
        }
        uint8_t data[4][sizeof(tag->memory[0])];
        for (uint8_t index = 0u; index < 4u; index++)
        {
            /* READ wraps across the final page, unlike FAST_READ. */
            uint8_t page = (uint8_t)(start_page + index);
            if (page >= page_count)
            {
                page -= page_count;
            }
            memcpy(data[index], tag->memory[page], sizeof(data[index]));
        }
        (void)nfca_response_data_crc(response, &data[0][0], sizeof(data));
        return;
    }

    default:
        break;
    }
    nfca_response_short4(response, NFCA_NACK_INVALID);
}

static void __not_in_flash_func(demo_ntag_handle)(
    void *context, const nfca_tag_request_t *request, nfca_response_t *response)
{
    demo_ntag_t *tag = context;
    nfca_activation_event_t event;
    /* The shared activation module handles REQA/WUPA, both cascade levels,
     * SELECT and HALT. Card commands are accepted only after activation. */
    const nfca_activation_result_t activation =
        nfca_activation_handle(&tag->activation, request, response, &event);
    if (activation != NFCA_ACTIVATION_UNHANDLED ||
        !nfca_activation_is_active(&tag->activation))
    {
        return;
    }
    if (request->short_frame || !request->parity_ok || !request->crc_ok ||
        request->size_bytes == 0u)
    {
        return;
    }
    demo_ntag_command(tag, request, response);
}

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    stdio_init_all();
    if (!demo_ntag_init(&demo_tag))
    {
        puts("Invalid NTAG identity: check UID and BCC bytes in pages 0..2.");
        while (true)
        {
            tight_loop_contents();
        }
    }
    nfc_config_t config = nfc_config_default();
    config.transport = PNU_EXAMPLE_TRANSPORT;
    if (nfc_init(&config) != NFC_OK)
    {
        puts(
            "PN532 initialization failed. Check wiring and SPI/HSU selection.");
        (void)nfc_stop();
        while (true)
        {
            tight_loop_contents();
        }
    }
    puts("NTAG Custom Handler: starting read-only NTAG213 emulation.");
    if (nfca_emulation_start(demo_ntag_handle, &demo_tag) != NFC_OK)
    {
        puts("NFC-A emulation start failed.");
        (void)nfc_stop();
        while (true)
        {
            tight_loop_contents();
        }
    }
    /* The context remains alive and handler-owned. No printing or host work
     * enters the RF callback. The engine handles capture, FDT and modulation.
     */
    while (true)
    {
        nfc_poll();
        tight_loop_contents();
    }
}
