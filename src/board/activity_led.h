#pragma once

#include "board/board_config.h"

/* Optional RP2040 Zero WS2812. Owns GPIO16, PIO1 SM2 and four instructions
 * until reset. Init/clear/task run on the RF owner's foreground core. Record
 * only after physical TX/RX completes; it stores bounded RAM events and is
 * safe after tag response transmission in the RX IRQ. TX/RX are from the
 * RP2040's perspective. No LED FIFO writes occur in record(). */
#if PNU_ACTIVITY_LED
bool board_activity_led_available(void);
void board_activity_led_init(void);
void board_activity_led_record(bool transmitted, bool received);
void board_activity_led_task(void);
/* Call after stopping RF. Off is queued by task without blocking, respecting
 * the WS2812 reset gap; nfc_poll must continue to run. */
void board_activity_led_clear(void);
#else
static inline bool board_activity_led_available(void)
{
    return true;
}
static inline void board_activity_led_init(void)
{
}
static inline void board_activity_led_record(bool transmitted, bool received)
{
    (void)transmitted;
    (void)received;
}
static inline void board_activity_led_task(void)
{
}
static inline void board_activity_led_clear(void)
{
}
#endif
