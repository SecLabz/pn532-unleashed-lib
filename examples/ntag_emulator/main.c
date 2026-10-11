#include "pn532_unleashed.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* One four-byte page per row. Keep BCC0/BCC1 consistent with the UID in
 * pages 0/1. Pages 4..39 contain raw data, not an NDEF message.
 * Locks are clear and AUTH0 is FF: the demo permits unprotected user writes.
 * clang-format is disabled only for the one-page-per-line memory table. */
// clang-format off
static const uint8_t memory[45][4] =
{
    {0x04, 0x11, 0x22, 0xbf}, /* Page  0 */
    {0x33, 0x44, 0x55, 0x66}, /* Page  1 */
    {0x44, 0x48, 0x00, 0x00}, /* Page  2 */
    {0xe1, 0x10, 0x12, 0x00}, /* Page  3 */
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
    {0x00, 0x00, 0x00, 0x00}, /* Page 40 */
    {0x00, 0x00, 0x00, 0xff}, /* Page 41 */
    {0x00, 0x00, 0x00, 0x00}, /* Page 42 */
    {0xff, 0xff, 0xff, 0xff}, /* Page 43 */
    {0x00, 0x00, 0x00, 0x00}, /* Page 44 */
};
// clang-format on

/* Synthetic state: zero signature/counters/attempts, intact tearing flags.
 * The signature is demonstration data, not an authentic NXP signature. */
static const type2_state_t state = {
    .version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0f, 0x03},
    .tearing = {0xbd, 0xbd, 0xbd},
};

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    stdio_init_all();
    puts("NTAG213 demo: UID 04112233445566, writable user memory.");
    static uint8_t uid[7];
    memcpy(uid, memory[0], 3u);
    memcpy(&uid[3], memory[1], 4u);
    static card_image_t image = {.model = CARD_NTAG213,
                                 .uid = uid,
                                 .uid_size_bytes = sizeof(uid),
                                 .memory = &memory[0][0],
                                 .memory_size_bytes = sizeof(memory)};
    image.state.type2 = state;
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
    if (emulation_start(&image) != NFC_OK)
    {
        puts("NTAG emulation start failed.");
        (void)nfc_stop();
        while (true)
        {
            tight_loop_contents();
        }
    }
    /* Start copied the image; reader writes affect backend RAM only. */
    while (true)
    {
        nfc_poll();
        tight_loop_contents();
    }
}
