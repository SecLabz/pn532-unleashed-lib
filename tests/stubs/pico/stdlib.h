#pragma once

#include <stdbool.h>
#include <stdint.h>

void sleep_ms(uint32_t milliseconds);
void sleep_us(uint64_t microseconds);

uint32_t time_us_32(void);
uint64_t time_us_64(void);
void busy_wait_us_32(uint32_t delay_us);

static inline void tight_loop_contents(void)
{
}

bool stdio_init_all(void);
