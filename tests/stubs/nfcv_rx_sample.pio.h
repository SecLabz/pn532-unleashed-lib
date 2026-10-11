#pragma once

#include <hardware/pio.h>

static const struct pio_program nfcv_rx_sample_program = {0};

static inline pio_sm_config
nfcv_rx_sample_program_get_default_config(uint offset)
{
    (void)offset;
    return (pio_sm_config){0};
}
