#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t timelr;
} timer_hw_t;

extern timer_hw_t *timer_hw;
