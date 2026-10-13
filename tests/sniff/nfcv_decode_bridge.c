/* Production decoder boundary; samples and expected frames live in Python. */
#include "phy/nfcv/pcd/nfcv_pcd_decode.h"
#include "phy/nfcv/sniff/sniff_internal.h"
static nfcv_sniff_frame_t frames[64];
static unsigned count;
void nfcv_sniff_publish(const nfcv_sniff_frame_t *frame)
{
    if (count < 64u)
    {
        frames[count++] = *frame;
    }
}
void host_nfcv_sniff_reset(void)
{
    count = 0u;
    nfcv_sniff_decode_reset();
}
unsigned host_nfcv_sniff_count(void)
{
    return count;
}
const nfcv_sniff_frame_t *host_nfcv_sniff_frame(unsigned index)
{
    return index < count ? &frames[index] : 0;
}

static uint32_t maximum_scan_samples;
bool host_find_sof(const uint32_t *words, uint32_t count, uint32_t *start)
{
    if (count > maximum_scan_samples)
    {
        maximum_scan_samples = count;
    }
    return nfcv_find_sof(words, count, start);
}
bool host_decode_levels(const uint32_t *words,
                        uint32_t count,
                        uint8_t *data,
                        uint16_t capacity,
                        nfcv_level_result_t *result)
{
    if (count > maximum_scan_samples)
    {
        maximum_scan_samples = count;
    }
    return nfcv_decode_levels(words, count, data, capacity, result);
}
void host_scan_reset(void)
{
    maximum_scan_samples = 0u;
}
uint32_t host_maximum_scan_samples(void)
{
    return maximum_scan_samples;
}
