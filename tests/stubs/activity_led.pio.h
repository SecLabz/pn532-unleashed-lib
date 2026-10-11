#pragma once

#include "hardware/pio.h"

/* The native boundary substitutes only the SDK-generated PIO initializer;
 * the actual WS2812 program and divider are compiled in firmware builds. */
static const struct pio_program activity_led_program = {.length = 4u};
void activity_led_program_init(PIO pio, uint sm, uint offset, uint pin);
