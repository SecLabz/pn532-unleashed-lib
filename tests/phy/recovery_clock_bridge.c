/* PWM/time observations only; no RF or real-time scheduling qualification. */
#include "phy/nfca/pcd/nfca_recovery_clock.h"

#include "hardware/pwm.h"

#include <assert.h>

static uint32_t now_us;
static uint16_t raw;
static bool running;
static float divider;
static bool advance_us;
static bool advance_pwm;
static uint32_t counter_reads;

uint32_t host_recovery_clock_counter_reads(void)
{
    return counter_reads;
}

void host_recovery_clock_set(uint32_t time_us, uint16_t pwm_ticks)
{
    now_us = time_us;
    raw = pwm_ticks;
}

float host_recovery_clock_divider(void)
{
    return divider;
}

bool host_recovery_clock_running(void)
{
    return running;
}

void host_recovery_clock_advance(bool microseconds, bool counter)
{
    counter_reads = 0u;
    advance_us = microseconds;
    advance_pwm = counter;
}

uint32_t time_us_32(void)
{
    return advance_us ? now_us++ : now_us;
}

pwm_config pwm_get_default_config(void)
{
    return (pwm_config){0};
}

void pwm_config_set_clkdiv(pwm_config *config, float value)
{
    config->divider = value;
}

void pwm_config_set_wrap(pwm_config *config, uint16_t wrap)
{
    config->wrap = wrap;
}

void pwm_init(unsigned int slice, const pwm_config *config, bool enabled)
{
    assert(slice == 0u && config->wrap == UINT16_MAX);
    divider = config->divider;
    running = enabled;
    raw = 0u;
}

void pwm_set_enabled(unsigned int slice, bool enabled)
{
    assert(slice == 0u);
    running = enabled;
}

uint16_t pwm_get_counter(unsigned int slice)
{
    assert(slice == 0u);
    counter_reads++;
    return advance_pwm ? raw++ : raw;
}
