#include "pn532_unleashed.h"
#include "pn532_unleashed/nfca_sniff.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <inttypes.h>
#include <stdio.h>

#define CAPTURE_DURATION_MS 10000u
#define STORED_FRAMES 16u

static nfca_sniff_frame_t frames[STORED_FRAMES];
static unsigned frame_count;

static nfc_status_t read_frames(void)
{
    while (true)
    {
        nfca_sniff_frame_t frame;
        bool available;
        const nfc_status_t status = nfca_sniff_read(&frame, &available);
        if (status != NFC_OK || !available)
        {
            return status;
        }
        if (frame_count < STORED_FRAMES)
        {
            frames[frame_count++] = frame;
        }
        /* Keep draining after local storage fills so capture can continue. */
    }
}

static nfc_status_t capture(void)
{
    nfca_sniff_config_t config = nfca_sniff_config_default();
    config.aux1_gpio = 0u;
    config.aux2_gpio = 1u;
    nfc_status_t status = nfca_sniff_start(&config);
    if (status != NFC_OK)
    {
        if (status == NFC_RF_ERROR)
        {
            const nfc_status_t cleanup = nfc_stop();
            return cleanup != NFC_OK ? cleanup : status;
        }
        return status;
    }

    const absolute_time_t deadline = make_timeout_time_ms(CAPTURE_DURATION_MS);
    while (!time_reached(deadline) && nfc_state() == NFC_STATE_SNIFFING_NFCA)
    {
        nfc_poll();
        status = read_frames();
        if (status != NFC_OK)
        {
            break;
        }
    }
    if (nfc_state() == NFC_STATE_ERROR)
    {
        status = NFC_RF_ERROR;
    }
    const nfc_status_t cleanup = nfc_stop();
    if (cleanup != NFC_OK)
    {
        return cleanup;
    }
    /* Stop can publish the last partial frame. Read it before reporting. */
    const nfc_status_t drained = read_frames();
    return status != NFC_OK ? status : drained;
}

static void print_capture(void)
{
    printf("%12s | %-6s | %5s | %4s | %-9s | %-7s | %-13s | Data\n",
           "Time (fc)",
           "Source",
           "Bytes",
           "Bits",
           "Framing",
           "Parity",
           "CRC candidate");
    for (unsigned index = 0u; index < frame_count; index++)
    {
        const nfca_sniff_frame_t *frame = &frames[index];
        printf("%12llu | %-6s | %5u | %4u | %-9s | %-7s | %-13s |",
               (unsigned long long)frame->start_fc,
               frame->direction == NFCA_SNIFF_PCD ? "Reader" : "Tag",
               (unsigned)frame->size_bytes,
               (unsigned)frame->size_bits,
               (frame->flags & NFCA_SNIFF_TRUNCATED) != 0u ? "Truncated"
               : (frame->flags & NFCA_SNIFF_DECODED) != 0u ? "Complete"
                                                           : "Invalid",
               (frame->flags & NFCA_SNIFF_PARITY_CHECKED) == 0u ? "N/A"
               : (frame->flags & NFCA_SNIFF_PARITY_VALID) != 0u ? "Valid"
                                                                : "Invalid",
               (frame->flags & (NFCA_SNIFF_DECODED | NFCA_SNIFF_CRC_CHECKED)) !=
                       (NFCA_SNIFF_DECODED | NFCA_SNIFF_CRC_CHECKED)
                   ? "N/A"
               : (frame->flags & NFCA_SNIFF_CRC_VALID) != 0u ? "Valid"
                                                             : "Invalid");
        for (uint16_t byte = 0u; byte < frame->size_bytes; byte++)
        {
            printf(" %02X", (unsigned)frame->data[byte]);
        }
        putchar('\n');
    }
    nfca_sniff_summary_t summary;
    if (nfca_sniff_summary(&summary) == NFC_OK)
    {
        printf("Reader %" PRIu32 " | Tag %" PRIu32 " | Invalid %" PRIu32
               " | Dropped %" PRIu32 " | Overruns %" PRIu32 "\n",
               summary.pcd_frames,
               summary.picc_frames,
               summary.invalid_frames,
               summary.dropped_frames,
               summary.capture_overruns);
    }
    printf("Displayed the first %u frames.\n", frame_count);
}

int main(void)
{
    if (!set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true))
    {
        return 1;
    }
    stdio_init_all();
    nfc_config_t config = nfc_config_default();
    config.transport = PNU_EXAMPLE_TRANSPORT;
    if (nfc_init(&config) != NFC_OK)
    {
        (void)nfc_stop();
        puts(
            "PN532 initialization failed. Check wiring and SPI/HSU selection.");
        return 1;
    }
    puts("NFC-A sniffer: press Enter to capture for 10 seconds.");
    int key;
    do
    {
        key = getchar();
    } while (key != '\r' && key != '\n');
    puts("Capturing for 10 seconds; results appear after capture stops.");
    const nfc_status_t status = capture();
    print_capture();
    if (status == NFC_OK)
    {
        puts("Capture complete. Reset to run again.");
    }
    else
    {
        printf("Capture failed (status %u). Reset to run again.\n",
               (unsigned)status);
    }
    while (true)
    {
        tight_loop_contents();
    }
}
