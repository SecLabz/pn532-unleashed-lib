#include "pn532_unleashed.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <stdio.h>

/* One four-byte block per row. The locks array has one 0/1 byte per block.
 * This synthetic SLIX starts with unlocked memory and no EAS/AFI protection. */
// clang-format off
static const uint8_t memory[28][4] =
{
    {0x00, 0x01, 0x02, 0x03}, /* Block  0 */
    {0x04, 0x05, 0x06, 0x07}, /* Block  1 */
    {0x08, 0x09, 0x0a, 0x0b}, /* Block  2 */
    {0x0c, 0x0d, 0x0e, 0x0f}, /* Block  3 */
    {0x10, 0x11, 0x12, 0x13}, /* Block  4 */
    {0x14, 0x15, 0x16, 0x17}, /* Block  5 */
    {0x18, 0x19, 0x1a, 0x1b}, /* Block  6 */
    {0x1c, 0x1d, 0x1e, 0x1f}, /* Block  7 */
    {0x20, 0x21, 0x22, 0x23}, /* Block  8 */
    {0x24, 0x25, 0x26, 0x27}, /* Block  9 */
    {0x28, 0x29, 0x2a, 0x2b}, /* Block 10 */
    {0x2c, 0x2d, 0x2e, 0x2f}, /* Block 11 */
    {0x30, 0x31, 0x32, 0x33}, /* Block 12 */
    {0x34, 0x35, 0x36, 0x37}, /* Block 13 */
    {0x38, 0x39, 0x3a, 0x3b}, /* Block 14 */
    {0x3c, 0x3d, 0x3e, 0x3f}, /* Block 15 */
    {0x40, 0x41, 0x42, 0x43}, /* Block 16 */
    {0x44, 0x45, 0x46, 0x47}, /* Block 17 */
    {0x48, 0x49, 0x4a, 0x4b}, /* Block 18 */
    {0x4c, 0x4d, 0x4e, 0x4f}, /* Block 19 */
    {0x50, 0x51, 0x52, 0x53}, /* Block 20 */
    {0x54, 0x55, 0x56, 0x57}, /* Block 21 */
    {0x58, 0x59, 0x5a, 0x5b}, /* Block 22 */
    {0x5c, 0x5d, 0x5e, 0x5f}, /* Block 23 */
    {0x60, 0x61, 0x62, 0x63}, /* Block 24 */
    {0x64, 0x65, 0x66, 0x67}, /* Block 25 */
    {0x68, 0x69, 0x6a, 0x6b}, /* Block 26 */
    {0x6c, 0x6d, 0x6e, 0x6f}, /* Block 27 */
};
// clang-format on
static const uint8_t locks[28] = {0};

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    stdio_init_all();
    puts("SLIX demo: UID E004010102030405, writable memory.");
    static const uint8_t uid[8] = {
        0xe0, 0x04, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05};
    static const card_image_t image = {.model = CARD_SLIX,
                                       .uid = uid,
                                       .uid_size_bytes = sizeof(uid),
                                       .memory = &memory[0][0],
                                       .memory_size_bytes = sizeof(memory),
                                       .state.nfcv = {.locks = locks,
                                                      .block_count = 28,
                                                      .dsfid = 0x12,
                                                      .afi = 0x34,
                                                      .ic_ref = 1}};
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
        puts("SLIX emulation start failed.");
        (void)nfc_stop();
        while (true)
        {
            tight_loop_contents();
        }
    }
    /* Responses run in the RX IRQ; poll updates the optional activity LED.
     * Start copied the image; reader writes affect backend RAM only. */
    while (true)
    {
        nfc_poll();
        tight_loop_contents();
    }
}
