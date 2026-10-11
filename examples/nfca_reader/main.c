#include "pn532_unleashed.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#ifndef PNU_EXAMPLE_TRANSPORT
#define PNU_EXAMPLE_TRANSPORT NFC_TRANSPORT_SPI
#endif

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    nfc_config_t config = nfc_config_default();
    config.transport = PNU_EXAMPLE_TRANSPORT;
    if (nfc_init(&config) != NFC_OK)
    {
        return 1;
    }

    static nfca_dump_t dump;
    nfca_tag_t tag;
    nfc_status_t status = nfca_scan(&tag);
    if (status == NFC_OK)
    {
        status = nfca_select(&tag, CARD_NTAG213);
        if (status == NFC_OK)
        {
            status = nfca_read_all(NULL, &dump);
        }
    }
    const nfc_status_t stop_status = nfc_stop();
    return status == NFC_OK && stop_status == NFC_OK ? 0 : 1;
}
