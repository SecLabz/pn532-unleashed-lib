#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Reader-only reuse of PWM0, already reserved for RF. Units are fc/16 ticks.
 * Calls are foreground-only. Stop before another RF role uses PWM0. Deadline
 * distances must be in 1..65536 ticks; late or stalled deadlines fail.
 */
void nfca_recovery_clock_start(void);
void nfca_recovery_clock_stop(void);
uint32_t nfca_recovery_clock_now(void);
bool nfca_recovery_clock_wait(uint32_t deadline_ticks);
