#include "pn532_unleashed/diagnostics.h"

#include <assert.h>
#include <stdbool.h>

static bool active;
static uint8_t selected_gsn = 0x08u;
static unsigned gsn_calls;

bool pn532_frontend_trace_set_gsn(uint8_t value)
{
    gsn_calls++;
    if (active)
    {
        return false;
    }
    selected_gsn = value;
    return true;
}

uint8_t nfc_trace_gsn(void)
{
    return selected_gsn;
}

int main(void)
{
    /* Validate both values before mutating either bench setting. */
    assert(nfc_trace_configure(0x0fu, 900u) == NFC_INVALID_ARGUMENT);
    assert(nfc_trace_configure(0x80u, 901u) == NFC_INVALID_ARGUMENT);
    assert(gsn_calls == 0u);
    assert(selected_gsn == 0x08u && nfc_trace_fdt_fc() == 996u);

    active = true;
    assert(nfc_trace_configure(0x80u, 900u) == NFC_BUSY);
    assert(selected_gsn == 0x08u && nfc_trace_fdt_fc() == 996u);

    active = false;
    assert(nfc_trace_configure(0x80u, 900u) == NFC_OK);
    assert(nfc_trace_gsn() == 0x80u && nfc_trace_fdt_fc() == 900u);
    assert(nfc_trace_configure(0xf0u, 996u) == NFC_OK);
    assert(nfc_trace_gsn() == 0xf0u && nfc_trace_fdt_fc() == 996u);
    return 0;
}
