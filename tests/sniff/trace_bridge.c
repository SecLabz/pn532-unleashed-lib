#include "phy/nfca/sniff/sniff_internal.h"
#include "pn532_unleashed/core.h"
static nfc_state_t state = NFC_STATE_IDLE;
nfc_state_t nfc_state(void)
{
    return state;
}
void host_sniff_trace_state(unsigned selected)
{
    state = (nfc_state_t)selected;
}
void host_sniff_trace_words(unsigned count,
                            uint32_t envelope,
                            uint32_t data,
                            uint32_t active,
                            uint32_t flags)
{
    const uint32_t levels[3] = {envelope, data, active};
    for (unsigned i = 0; i < count; i++)
    {
        nfca_sniff_trace_word(levels, flags);
    }
}
