#include "phy/nfca/pcd/nfca_recovery_clock.h"

#include "board/board_config.h"
#include "phy/pcd/reader_rf_trace.h"

#include "hardware/pwm.h"
#include "pico/stdlib.h"

static struct
{
    uint32_t ticks;
    uint32_t last_us;
    uint16_t raw;
} clock_state;

void nfca_recovery_clock_start(void)
{
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 16u * NFC_SYS_CLOCKS_PER_FC);
    pwm_config_set_wrap(&config, UINT16_MAX);
    pwm_init(NFCA_TIMER_SLICE, &config, true);
    clock_state.ticks = 0u;
    clock_state.raw = pwm_get_counter(NFCA_TIMER_SLICE);
    clock_state.last_us = time_us_32();
}

void nfca_recovery_clock_stop(void)
{
    pwm_set_enabled(NFCA_TIMER_SLICE, false);
    clock_state.ticks = 0u;
    clock_state.raw = 0u;
    clock_state.last_us = 0u;
}

uint32_t __not_in_flash_func(nfca_recovery_clock_now)(void)
{
    const uint32_t now_us = time_us_32();
    const uint16_t raw = pwm_get_counter(NFCA_TIMER_SLICE);
    const uint32_t elapsed_us = now_us - clock_state.last_us;
    uint32_t elapsed_ticks = (uint16_t)(raw - clock_state.raw);
    /* USB or host computation may span several 77 ms PWM wraps. Recover the
     * high word from the independent microsecond clock, keeping PWM phase.
     * The common timing loop avoids this division entirely. */
    if (elapsed_us >= 60000u)
    {
        const uint32_t estimate = (uint64_t)elapsed_us * 339u / 400u;
        elapsed_ticks += ((estimate + 32768u - elapsed_ticks) >> 16u) << 16u;
    }
    clock_state.ticks += elapsed_ticks;
    clock_state.raw = raw;
    clock_state.last_us = now_us;
    return clock_state.ticks;
}

bool __not_in_flash_func(nfca_recovery_clock_wait)(uint32_t deadline_ticks)
{
    const int32_t remaining_ticks =
        (int32_t)(deadline_ticks - nfca_recovery_clock_now());
    if (remaining_ticks <= 0 || remaining_ticks > 65536)
    {
        READER_RF_TRACE_REASON(remaining_ticks <= 0
                                   ? READER_TRACE_DEADLINE_LATE
                                   : READER_TRACE_DEADLINE_TOO_FAR);
        return false;
    }
    const uint32_t started_us = clock_state.last_us;
    const uint32_t timeout_us = (uint32_t)remaining_ticks * 400u / 339u + 1000u;
    uint16_t previous_raw_ticks = clock_state.raw;
    uint32_t remaining_ticks_left = (uint32_t)remaining_ticks;
    uint8_t timeout_polls = 0u;
    for (;;)
    {
        /* The extended clock is needed across host gaps, not in this final
         * wait. Poll only PWM on the hot path: timestamp reads and context
         * updates otherwise make the TX deadline overshoot vary by PRNG ticks.
         * Accumulating raw deltas also handles a full 65536-tick wait.
         */
        const uint16_t raw_ticks = pwm_get_counter(NFCA_TIMER_SLICE);
        const uint16_t elapsed_ticks =
            (uint16_t)(raw_ticks - previous_raw_ticks);
        if (elapsed_ticks >= remaining_ticks_left)
        {
            return true;
        }
        remaining_ticks_left -= elapsed_ticks;
        previous_raw_ticks = raw_ticks;
        /* Bound a stopped PWM without reading the microsecond timer on every
         * carrier-clock poll. The next extended-clock read recovers wraps.
         */
        if (++timeout_polls == 0u && time_us_32() - started_us > timeout_us)
        {
            READER_RF_TRACE_REASON(READER_TRACE_CLOCK_STALLED);
            return false;
        }
        tight_loop_contents();
    }
}
