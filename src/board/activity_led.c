#include "board/activity_led.h"

#include "activity_led.pio.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

#define ACTIVITY_LED_RED_GRB 0x000800u
#define ACTIVITY_LED_BLUE_GRB 0x000008u
#define ACTIVITY_LED_ON_US 15000u
#define ACTIVITY_LED_CYCLE_US 25000u
#define ACTIVITY_LED_RECENT_US 50000u
/* One 24-bit frame takes 30 us. Leave at least 80 us of reset-low, even
 * when stop/clear follows a color change immediately. */
#define ACTIVITY_LED_WRITE_GAP_US 120u

typedef struct
{
    volatile uint32_t recorded_us;
    volatile uint32_t sequence;
    uint32_t observed_sequence;
    uint32_t observed_us;
    bool active;
} activity_event_t;

static struct
{
    activity_event_t tx;
    activity_event_t rx;
    bool initialized;
    bool burst;
    uint32_t burst_start_us;
    uint32_t last_write_us;
    uint32_t color_grb;
} led;

bool board_activity_led_available(void)
{
    return !pio_sm_is_claimed(ACTIVITY_LED_PIO, ACTIVITY_LED_SM) &&
           pio_can_add_program(ACTIVITY_LED_PIO, &activity_led_program);
}

void board_activity_led_init(void)
{
    const uint offset =
        pio_add_program(ACTIVITY_LED_PIO, &activity_led_program);
    activity_led_program_init(
        ACTIVITY_LED_PIO, ACTIVITY_LED_SM, offset, ACTIVITY_LED_GPIO);
    led.tx = (activity_event_t){0};
    led.rx = (activity_event_t){0};
    led.burst = false;
    led.burst_start_us = 0u;
    led.color_grb = 0u;
    led.last_write_us = time_us_32();
    pio_sm_put(ACTIVITY_LED_PIO, ACTIVITY_LED_SM, 0u);
    led.initialized = true;
}

void __not_in_flash_func(board_activity_led_record)(bool transmitted,
                                                    bool received)
{
    const uint32_t now_us = timer_hw->timelr;
    if (transmitted)
    {
        led.tx.recorded_us = now_us;
        led.tx.sequence++;
    }
    if (received)
    {
        led.rx.recorded_us = now_us;
        led.rx.sequence++;
    }
}

static bool event_recent(activity_event_t *event, uint32_t now_us)
{
    /* One RF role owns writes. Read sequence before timestamp so an IRQ
     * arriving during this snapshot remains visible on the next task call.
     * No interrupt masking is needed for the cosmetic event snapshot. */
    const uint32_t sequence = event->sequence;
    if (sequence != event->observed_sequence)
    {
        event->observed_us = event->recorded_us;
        event->observed_sequence = sequence;
        event->active = true;
    }
    /* An RX IRQ may timestamp an event just after task sampled now_us. */
    if ((int32_t)(now_us - event->observed_us) >=
        (int32_t)ACTIVITY_LED_RECENT_US)
    {
        event->active = false;
    }
    return event->active;
}

void board_activity_led_task(void)
{
    if (!led.initialized)
    {
        return;
    }
    const uint32_t now_us = time_us_32();
    const bool tx = event_recent(&led.tx, now_us);
    const bool rx = event_recent(&led.rx, now_us);
    uint32_t color_grb = 0u;
    if (tx || rx)
    {
        if (!led.burst)
        {
            led.burst = true;
            led.burst_start_us = now_us;
        }
        const uint32_t elapsed_us = now_us - led.burst_start_us;
        if (elapsed_us % ACTIVITY_LED_CYCLE_US < ACTIVITY_LED_ON_US)
        {
            const bool show_tx =
                tx && (!rx || (elapsed_us / ACTIVITY_LED_CYCLE_US & 1u) == 0u);
            color_grb = show_tx ? ACTIVITY_LED_RED_GRB : ACTIVITY_LED_BLUE_GRB;
        }
    }
    else
    {
        led.burst = false;
    }
    if (color_grb != led.color_grb &&
        (uint32_t)(now_us - led.last_write_us) >= ACTIVITY_LED_WRITE_GAP_US &&
        !pio_sm_is_tx_fifo_full(ACTIVITY_LED_PIO, ACTIVITY_LED_SM))
    {
        pio_sm_put(ACTIVITY_LED_PIO, ACTIVITY_LED_SM, color_grb << 8u);
        led.color_grb = color_grb;
        led.last_write_us = now_us;
    }
}

void board_activity_led_clear(void)
{
    led.tx.observed_sequence = led.tx.sequence;
    led.rx.observed_sequence = led.rx.sequence;
    led.tx.active = false;
    led.rx.active = false;
    led.burst = false;
    board_activity_led_task();
}
