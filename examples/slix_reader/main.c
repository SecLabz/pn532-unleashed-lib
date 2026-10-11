#include "pn532_unleashed.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <stdio.h>

static void print_dump(const nfcv_slix_dump_t *dump)
{
    printf("\nSLIX tag UID: ");
    for (size_t byte = 0u; byte < sizeof(dump->tag.uid); byte++)
    {
        printf("%02X", dump->tag.uid[byte]);
    }
    printf("\nDSFID: %02X  AFI: %02X  IC reference: %02X\n",
           dump->tag.dsfid,
           dump->tag.afi,
           dump->tag.ic_ref);
    printf("Memory: %u blocks x %u bytes\n",
           dump->tag.block_count,
           dump->tag.block_size_bytes);
    for (uint8_t block = 0u; block < dump->tag.block_count; block++)
    {
        printf("Block %02u [%s]:",
               block,
               dump->security[block] != 0u ? "locked" : "open");
        for (uint8_t byte = 0u; byte < dump->tag.block_size_bytes; byte++)
        {
            printf(" %02X",
                   dump->memory[block * dump->tag.block_size_bytes + byte]);
        }
        putchar('\n');
    }
}

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    stdio_init_all();
    puts("SLIX reader: present one SLIX or SLIX-L tag.");
    nfc_config_t config = nfc_config_default();
    config.transport = PNU_EXAMPLE_TRANSPORT;
    if (nfc_init(&config) != NFC_OK)
    {
        puts(
            "PN532 initialization failed. Check wiring and SPI/HSU selection.");
        while (true)
        {
            tight_loop_contents();
        }
    }
    static nfcv_slix_dump_t dump;
    while (true)
    {
        const nfc_status_t status = nfcv_slix_read(&dump);
        /* Read has released RF before returning. Printing and waiting here
         * cannot delay a card response or expose a partial memory dump. */
        if (status == NFC_NO_TAG)
        {
            puts("Waiting for a tag...");
            sleep_ms(1000u);
            continue;
        }
        if (status != NFC_OK)
        {
            printf("Read failed (status %u).\n", (unsigned int)status);
            if (nfc_state() == NFC_STATE_ERROR && nfc_stop() != NFC_OK)
            {
                puts("RF cleanup failed. Check the PN532 and reset the board.");
                while (true)
                {
                    tight_loop_contents();
                }
            }
            sleep_ms(1000u);
            continue;
        }
        print_dump(&dump);
        sleep_ms(1000u);
    }
}
