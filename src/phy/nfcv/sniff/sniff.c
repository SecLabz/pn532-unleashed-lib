#include "api_internal.h"
#include "board/board_config.h"
#include "phy/nfcv/sniff/sniff_internal.h"
#include "pn532/pn532_command.h"
#include "pn532/pn532_frontend.h"
#include "pn532/pn532_registers.h"

#include <string.h>

/* Absorb short candidate bursts while the host receives the previous batch.
 * Capture never waits for USB or terminal output. */
#define SNIFF_QUEUE_FRAMES 16u
#define SNIFF_POLL_WORDS 64u
static const uint16_t aux_registers[] = {
    CIU_TEST_SEL1, CIU_TEST_SEL2, CIU_ANALOG_TEST};
static struct
{
    nfcv_sniff_frame_t queue[SNIFF_QUEUE_FRAMES];
    nfcv_sniff_summary_t summary;
    uint8_t old_aux[3];
    uint8_t head;
    uint8_t count;
    bool saved;
    bool active;
    bool started;
    bool owns_rf;
} sniff;

nfcv_sniff_config_t nfcv_sniff_config_default(void)
{
    return (nfcv_sniff_config_t){.aux1_gpio = 0u};
}

void nfcv_sniff_publish(const nfcv_sniff_frame_t *frame)
{
    if (frame->direction == NFCV_SNIFF_PCD)
    {
        sniff.summary.pcd_frames++;
    }
    else
    {
        sniff.summary.picc_frames++;
    }
    if ((frame->flags & NFCV_SNIFF_DECODED) == 0u)
    {
        sniff.summary.invalid_frames++;
    }
    if (sniff.count == SNIFF_QUEUE_FRAMES)
    {
        sniff.summary.dropped_frames++;
        return;
    }
    sniff.queue[(sniff.head + sniff.count) % SNIFF_QUEUE_FRAMES] = *frame;
    sniff.count++;
}

bool nfcv_sniff_stop(void)
{
    if (!sniff.owns_rf && !sniff.saved)
    {
        return true;
    }
    if (sniff.active)
    {
        sniff_capture_pause();
        uint32_t levels[3];
        for (unsigned i = 0u; i < 2048u && sniff_capture_read(levels); i++)
        {
            nfcv_sniff_decode_word(levels);
        }
        nfcv_sniff_decode_stop();
        sniff.active = false;
        if (sniff_capture_overrun())
        {
            sniff.summary.capture_overruns++;
        }
    }
    sniff_capture_stop();
    sniff.owns_rf = false;
    if (!sniff.saved)
    {
        return true;
    }
    bool restored = true;
    for (unsigned i = 0u; i < sizeof(aux_registers) / sizeof(aux_registers[0]);
         i++)
    {
        restored = pn532_write_register(aux_registers[i], sniff.old_aux[i]) &&
                   restored;
    }
    if (restored)
    {
        sniff.saved = false;
    }
    return restored;
}

nfc_status_t nfcv_sniff_start(const nfcv_sniff_config_t *config)
{
    const nfcv_sniff_config_t selected =
        config == NULL ? nfcv_sniff_config_default() : *config;
    nfc_config_t check = board_config.pins;
    check.sigin_gpio = selected.aux1_gpio;
    if (selected.aux1_gpio >= 30u || selected.aux1_gpio == TAG_LOAD_GPIO ||
        !board_config_validate(&check))
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_operation_begin(NFC_STATE_SNIFFING_NFCV);
    if (status != NFC_OK)
    {
        return status;
    }
    memset(&sniff, 0, sizeof(sniff));
    sniff.started = true;
    sniff.owns_rf = true;
    nfcv_sniff_decode_reset();
    for (unsigned i = 0u; i < 3u; i++)
    {
        if (!pn532_read_register(aux_registers[i], &sniff.old_aux[i]))
        {
            nfc_operation_end(NFC_RF_ERROR);
            return NFC_RF_ERROR;
        }
    }
    sniff.saved = true;
    const uint8_t settings[] = {
        (sniff.old_aux[0] & 0xf8u) | 3u,
        0x02u,
        (sniff.old_aux[2] & 0x0fu) | 0xf0u,
    };
    if (!pn532_frontend_prepare_nfcv_sniff())
    {
        nfc_operation_end(NFC_RF_ERROR);
        return NFC_RF_ERROR;
    }
    for (unsigned i = 0u; i < 3u; i++)
    {
        uint8_t observed;
        if (!pn532_write_register(aux_registers[i], settings[i]) ||
            !pn532_read_register(aux_registers[i], &observed) ||
            observed != settings[i])
        {
            nfc_operation_end(NFC_RF_ERROR);
            return NFC_RF_ERROR;
        }
    }
    const uint8_t pins[] = {TAG_RX_GPIO, selected.aux1_gpio};
    if (!sniff_capture_start(pins, 2u))
    {
        const bool aux_restored = nfcv_sniff_stop();
        const bool frontend_stopped = pn532_frontend_stop();
        const bool restored = aux_restored && frontend_stopped;
        nfc_operation_end(restored ? NFC_OK : NFC_RF_ERROR);
        return restored ? NFC_RESOURCE_BUSY : NFC_RF_ERROR;
    }
    sniff.active = true;
    return NFC_OK;
}

void nfcv_sniff_poll(void)
{
    if (!sniff.active)
    {
        return;
    }
    uint32_t levels[3];
    for (unsigned i = 0u; i < SNIFF_POLL_WORDS; i++)
    {
        if (!sniff_capture_read(levels))
        {
            break;
        }
        nfcv_sniff_decode_word(levels);
    }
    if (sniff_capture_overrun())
    {
        sniff.summary.capture_overruns++;
        sniff_capture_stop();
        nfcv_sniff_decode_stop();
        sniff.active = false;
        nfc_operation_end(NFC_RF_ERROR);
    }
}

nfc_status_t nfcv_sniff_read(nfcv_sniff_frame_t *frame, bool *available)
{
    if (frame == NULL || available == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (!sniff.started)
    {
        return NFC_BUSY;
    }
    *available = sniff.count != 0u;
    if (*available)
    {
        *frame = sniff.queue[sniff.head];
        sniff.head = (sniff.head + 1u) % SNIFF_QUEUE_FRAMES;
        sniff.count--;
    }
    return NFC_OK;
}

nfc_status_t nfcv_sniff_summary(nfcv_sniff_summary_t *summary)
{
    if (summary == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (!sniff.started)
    {
        return NFC_BUSY;
    }
    *summary = sniff.summary;
    return NFC_OK;
}

bool nfcv_sniff_requires_stop(void)
{
    return sniff.owns_rf || sniff.saved;
}
