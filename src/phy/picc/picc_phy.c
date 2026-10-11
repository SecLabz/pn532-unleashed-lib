#include "phy/picc/picc_phy.h"

#include "board/tag_board.h"
#include "phy/picc/picc_tx_stream.h"
#include "pico.h"

#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/structs/pwm.h"
#include "pico/stdlib.h"

#define PWM_TIMER_SLICE NFCA_TIMER_SLICE
#define PWM_TIMER_IRQ_MASK (1u << PWM_TIMER_SLICE)

void tag_rx_pio_prepare_input(void)
{
    pio_gpio_init(TAG_RX_PIO, TAG_RX_GPIO);
    gpio_set_pulls(TAG_RX_GPIO, TAG_RX_PULL_UP, TAG_RX_PULL_DOWN);
    gpio_set_inover(TAG_RX_GPIO, GPIO_OVERRIDE_NORMAL);
}

void __not_in_flash_func(tag_rx_pio_stop)(void)
{
    pio_sm_set_enabled(TAG_RX_PIO, TAG_RX_SM, false);
}

void __not_in_flash_func(tag_rx_pio_start)(void)
{
    pio_sm_set_enabled(TAG_RX_PIO, TAG_RX_SM, true);
}

void __not_in_flash_func(tag_rx_pio_stop_and_drain)(void)
{
    pio_sm_set_enabled(TAG_RX_PIO, TAG_RX_SM, false);
    while (!pio_sm_is_rx_fifo_empty(TAG_RX_PIO, TAG_RX_SM))
    {
        (void)pio_sm_get(TAG_RX_PIO, TAG_RX_SM);
    }
}

void __not_in_flash_func(tag_rx_pio_reset)(uint32_t program_offset)
{
    pio_sm_set_enabled(TAG_RX_PIO, TAG_RX_SM, false);
    pio_sm_clear_fifos(TAG_RX_PIO, TAG_RX_SM);
    pio_sm_restart(TAG_RX_PIO, TAG_RX_SM);
    pio_sm_clkdiv_restart(TAG_RX_PIO, TAG_RX_SM);
    pio_sm_exec(TAG_RX_PIO, TAG_RX_SM, pio_encode_jmp((uint)program_offset));
}

void __not_in_flash_func(tag_rx_pio_rearm)(uint32_t program_offset)
{
    tag_rx_pio_reset(program_offset);
    tag_rx_pio_start();
}

void __not_in_flash_func(tag_rx_pio_fifo_irq_set_enabled)(bool enabled)
{
    pio_set_irq0_source_enabled(
        TAG_RX_PIO, pis_sm0_rx_fifo_not_empty + TAG_RX_SM, enabled);
}

static void picc_phy_timer_stop(void);

void tag_phy_stop(void)
{
    picc_phy_timer_stop();
    tag_tx_stream_stop();
}

static void __not_in_flash_func(picc_phy_timer_stop)(void)
{
    pwm_hw->slice[PWM_TIMER_SLICE].csr = 0;
    pwm_hw->intr = PWM_TIMER_IRQ_MASK;
}

static void tag_phy_init_common(void)
{
    gpio_init(TAG_LOAD_GPIO);
    gpio_set_dir(TAG_LOAD_GPIO, GPIO_OUT);
    gpio_set_drive_strength(TAG_LOAD_GPIO, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(TAG_LOAD_GPIO, GPIO_SLEW_RATE_FAST);

    /*
     * The v3 load switch is driven directly as an active-high load control.
     */
    gpio_set_outover(TAG_LOAD_GPIO, GPIO_OVERRIDE_NORMAL);
    gpio_put(TAG_LOAD_GPIO, false);

    pwm_hw->slice[PWM_TIMER_SLICE].csr = 0;
    pwm_hw->intr = PWM_TIMER_IRQ_MASK;
    /* The stopwatch counts system clocks even if the embedding application
     * configured this inactive slice before giving it to RF. */
    pwm_set_clkdiv_int_frac(PWM_TIMER_SLICE, 1u, 0u);
    pwm_set_irq_enabled(PWM_TIMER_SLICE, true);
}

bool tag_phy_init(void)
{
    tag_phy_init_common();
    return tag_tx_stream_init();
}

void tag_phy_init_sio(void)
{
    tag_phy_init_common();
}
