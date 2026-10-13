#include "phy/nfca/sniff/sniff_internal.h"
static nfca_sniff_frame_t frames[64];
static unsigned count;
void nfca_sniff_publish(const nfca_sniff_frame_t *frame)
{
    if (count < 64u)
    {
        frames[count++] = *frame;
    }
}
void host_sniff_reset(void)
{
    count = 0u;
    nfca_sniff_decode_reset();
}
unsigned host_sniff_count(void)
{
    return count;
}
const nfca_sniff_frame_t *host_sniff_frame(unsigned index)
{
    return index < count ? &frames[index] : 0;
}
