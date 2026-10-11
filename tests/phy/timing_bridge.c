#include "phy/nfca/picc/nfca_trace.h"
#include "phy/timing.h"

#include <stdio.h>

int main(void)
{
    uint8_t header[48];
    nfca_trace_reset();
    if (nfc_trace_nfca(0u, header, sizeof(header)) != sizeof(header))
    {
        return 1;
    }
    const uint32_t trace_clock =
        (uint32_t)header[4] | ((uint32_t)header[5] << 8u) |
        ((uint32_t)header[6] << 16u) | ((uint32_t)header[7] << 24u);
    printf("%u %u %u %u %u %.1f\n",
           trace_clock,
           NFC_8FC_SYS_CYCLES,
           NFC_128FC_SYS_CYCLES,
           NFC_FC_TO_SYS_CYCLES(996u),
           NFC_FC_TO_SYS_CYCLES(932u),
           (double)NFCA_RX_PIO_CLKDIV);
    return 0;
}
