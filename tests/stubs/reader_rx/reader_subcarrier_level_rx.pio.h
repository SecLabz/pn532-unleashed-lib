#pragma once

static const struct pio_program reader_subcarrier_level_rx_1fc_program = {0};
static const struct pio_program reader_subcarrier_level_rx_8fc_program = {0};

static inline pio_sm_config
reader_subcarrier_level_rx_1fc_program_get_default_config(uint offset)
{
    (void)offset;
    return (pio_sm_config){0};
}

static inline pio_sm_config
reader_subcarrier_level_rx_8fc_program_get_default_config(uint offset)
{
    (void)offset;
    return (pio_sm_config){0};
}
