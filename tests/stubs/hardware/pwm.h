#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t wrap;
    float divider;
} pwm_config;
pwm_config pwm_get_default_config(void);
void pwm_config_set_clkdiv(pwm_config *config, float divider);
void pwm_config_set_wrap(pwm_config *config, uint16_t wrap);
void pwm_init(unsigned int slice, const pwm_config *config, bool enabled);
void pwm_set_enabled(unsigned int slice, bool enabled);
uint16_t pwm_get_counter(unsigned int slice);
