/* Existing reader/emulation harnesses do not model passive capture hardware. */
#include "pn532_unleashed/nfca_sniff.h"

#include <stdbool.h>

void nfca_sniff_poll(void)
{
}
bool nfca_sniff_stop(void)
{
    return true;
}
bool nfca_sniff_requires_stop(void)
{
    return false;
}

void nfcv_sniff_poll(void)
{
}
bool nfcv_sniff_stop(void)
{
    return true;
}
bool nfcv_sniff_requires_stop(void)
{
    return false;
}
