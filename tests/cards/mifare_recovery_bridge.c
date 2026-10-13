#include "pn532_unleashed/core.h"
/* Host protocol peer substitutes RF cadence. It cannot qualify PIO timing. */
#include "cards/mifare/reader/mifare_recovery.h"
#include "cards/nfca_access.h"
#include "phy/nfca/pcd/nfca_recovery_clock.h"
#include "protocols/nfca/reader.h"

static uint32_t ticks;
static uint32_t irq_masked;
static bool darkside_peer;

void host_darkside_peer(bool enabled)
{
    darkside_peer = enabled;
}

nfc_status_t host_nfca_load_view(const card_image_t *image, uint8_t error);

nfc_status_t host_mifare_open_view(const card_image_t *image,
                                   card_model_t model,
                                   uint8_t fault)
{
    (void)nfc_stop();
    nfc_status_t status = host_nfca_load_view(image, fault);
    nfca_tag_t tag;
    if (status == NFC_OK)
    {
        status = nfca_reader_open_selected(&tag);
    }
    if (status != NFC_OK)
    {
        return status;
    }
    (void)nfca_reader_stop();
    return nfca_select(&tag, model);
}

void host_mifare_close(void)
{
    (void)nfc_stop();
}

bool host_mifare_interrupts_masked(void)
{
    return irq_masked != 0u;
}

uint32_t save_and_disable_interrupts(void)
{
    const uint32_t previous = irq_masked;
    irq_masked = 1u;
    return previous;
}

void restore_interrupts(uint32_t state)
{
    irq_masked = state;
}

uint32_t time_us_32(void)
{
    return ticks;
}

void nfca_recovery_clock_start(void)
{
    ticks = 0u;
}

void nfca_recovery_clock_stop(void)
{
    ticks = 0u;
}

uint32_t nfca_recovery_clock_now(void)
{
    return ticks++;
}

bool nfca_recovery_clock_wait(uint32_t deadline_ticks)
{
    if ((int32_t)(deadline_ticks - ticks) <= 0)
    {
        return false;
    }
    ticks = deadline_ticks;
    return true;
}

bool nfca_pcd_exchange_at(const reader_frame_t *tx,
                          nfca_pcd_response_t *rx,
                          uint16_t max_bits,
                          uint32_t deadline_ticks)
{
    if (deadline_ticks != 0u)
    {
        /* 360 us measured before scheduled AUTH on the 54.24 MHz bench. */
        ticks += 305u;
        if (!nfca_recovery_clock_wait(deadline_ticks))
        {
            return false;
        }
    }
    if (darkside_peer && deadline_ticks != 0u)
    {
        *rx = (nfca_pcd_response_t){0};
        if (tx->length == 4u)
        {
            rx->data[0] = 0x12u;
            rx->data[1] = 0x34u;
            rx->data[2] = 0x56u;
            rx->data[3] = 0x78u;
            rx->size_bits = 32u;
            rx->parity_valid = true;
            rx->framing_valid = true;
        }
        else if (tx->length == 8u)
        {
            uint8_t supplied = 0u;
            for (uint8_t index = 0u; index < 8u; index++)
            {
                supplied = (supplied << 1u) | tx->parity[index];
            }
            const uint8_t wanted = 0xa0u | ((tx->data[3] >> 5u) + 5u);
            if (supplied == wanted)
            {
                rx->data[0] = 0x05u;
                rx->size_bits = 4u;
                rx->framing_valid = true;
            }
        }
        return true;
    }
    return nfca_pcd_exchange(tx, rx, max_bits);
}

bool nfca_pcd_send(const reader_frame_t *tx)
{
    nfca_pcd_response_t rx;
    return nfca_pcd_exchange(tx, &rx, 8u);
}
