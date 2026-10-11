#pragma once

#include <stdbool.h>
#include <stdint.h>

enum clock_index
{
    clk_sys,
};

uint32_t clock_get_hz(enum clock_index clock);

bool set_sys_clock_khz(uint32_t frequency_khz, bool required);
