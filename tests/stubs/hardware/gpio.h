#pragma once

#include <stdbool.h>

#define GPIO_IN false
#define GPIO_OUT true
#define GPIO_DRIVE_STRENGTH_12MA 3u
#define GPIO_SLEW_RATE_FAST 1u

void gpio_init(unsigned int pin);
void gpio_disable_pulls(unsigned int pin);
void gpio_set_dir(unsigned int pin, bool output);
void gpio_put(unsigned int pin, bool value);
void gpio_set_drive_strength(unsigned int pin, unsigned int strength);
void gpio_set_slew_rate(unsigned int pin, unsigned int rate);
void gpio_set_pulls(unsigned int pin, bool up, bool down);
void gpio_set_input_hysteresis_enabled(unsigned int pin, bool enabled);
void gpio_set_inover(unsigned int pin, unsigned int value);
#define GPIO_OVERRIDE_NORMAL 0u
