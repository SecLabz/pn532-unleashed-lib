#include "board/board_config.h"
#include "pn532/pn532_frame.h"
#include "pn532/pn532_transport.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pn532_hsu.pio.h"

#ifdef PNU_HSU_TRACE
#include <stdio.h>
#endif

#define HSU_TIMEOUT_US 100000u
#define HSU_CYCLES_PER_BIT 8u
#define HSU_RX_DISCARD_MAX_BYTES 32u
#define HSU_WAKEUP_DELAY_MS 5u
#define HSU_RX_BYTE_SHIFT 24u
#define HSU_FRAMING_IRQ (4u + PN532_HSU_RX_SM)
#define HSU_RX_STALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB + PN532_HSU_RX_SM))

static bool configured;
static bool initialized;
static uint32_t read_started_us;

#ifdef PNU_HSU_TRACE
static struct
{
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t timeouts;
    uint32_t framing_errors;
    uint32_t rx_stalls;
    uint8_t prefix[24];
} trace;

uint16_t pn532_hsu_trace(char *output, uint16_t capacity)
{
    const int size = snprintf(output,
                              capacity,
                              "hsu_tx_bytes=%lu\nhsu_rx_bytes=%lu\n"
                              "hsu_timeouts=%lu\nhsu_framing_errors=%lu\n"
                              "hsu_rx_stalls=%lu\nhsu_tx_level=%u\n"
                              "hsu_rx_level=%u\nhsu_rx_prefix=",
                              (unsigned long)trace.tx_bytes,
                              (unsigned long)trace.rx_bytes,
                              (unsigned long)trace.timeouts,
                              (unsigned long)trace.framing_errors,
                              (unsigned long)trace.rx_stalls,
                              gpio_get(PN532_HSU_TX_GPIO) ? 1u : 0u,
                              gpio_get(PN532_HSU_RX_GPIO) ? 1u : 0u);
    if (size < 0 || (uint16_t)size >= capacity)
    {
        return 0u;
    }
    uint16_t used = (uint16_t)size;
    for (uint32_t i = 0u; i < trace.rx_bytes && i < sizeof(trace.prefix); i++)
    {
        const int added =
            snprintf(output + used, capacity - used, "%02X", trace.prefix[i]);
        if (added != 2 || (uint32_t)used + 2u >= capacity)
        {
            return 0u;
        }
        used += 2u;
    }
    if ((uint32_t)used + 1u >= capacity)
    {
        return 0u;
    }
    output[used++] = '\n';
    output[used] = '\0';
    return used;
}
#endif

static bool receive_failed(void)
{
    const bool framing = pio_interrupt_get(PN532_HSU_PIO, HSU_FRAMING_IRQ);
    const bool stalled = (PN532_HSU_PIO->fdebug & HSU_RX_STALL_MASK) != 0u;
    const bool timeout =
        (uint32_t)(time_us_32() - read_started_us) >= HSU_TIMEOUT_US;
#ifdef PNU_HSU_TRACE
    trace.framing_errors += framing ? 1u : 0u;
    trace.rx_stalls += stalled ? 1u : 0u;
    trace.timeouts += timeout ? 1u : 0u;
#endif
    return framing || stalled || timeout;
}

static bool discard_input(void)
{
    /* Bound stale-response cleanup even if a faulty peer keeps sending. */
    for (uint8_t index = 0u; index < HSU_RX_DISCARD_MAX_BYTES; index++)
    {
        if (pio_sm_is_rx_fifo_empty(PN532_HSU_PIO, PN532_HSU_RX_SM))
        {
            break;
        }
        (void)pio_sm_get(PN532_HSU_PIO, PN532_HSU_RX_SM);
    }
    pio_interrupt_clear(PN532_HSU_PIO, HSU_FRAMING_IRQ);
    PN532_HSU_PIO->fdebug = HSU_RX_STALL_MASK;
    return pio_sm_is_rx_fifo_empty(PN532_HSU_PIO, PN532_HSU_RX_SM);
}

static bool configure_pio(void)
{
    if (configured)
    {
        return true;
    }
    if (!pio_can_add_program(PN532_HSU_PIO, &pn532_hsu_tx_program))
    {
        return false;
    }
    const uint tx_offset =
        pio_add_program(PN532_HSU_PIO, &pn532_hsu_tx_program);
    if (!pio_can_add_program(PN532_HSU_PIO, &pn532_hsu_rx_program))
    {
        pio_remove_program(PN532_HSU_PIO, &pn532_hsu_tx_program, tx_offset);
        return false;
    }
    const uint rx_offset =
        pio_add_program(PN532_HSU_PIO, &pn532_hsu_rx_program);
    const float divider = (float)clock_get_hz(clk_sys) /
                          (float)(HSU_CYCLES_PER_BIT * PN532_HSU_BAUD);

    pio_sm_config tx = pn532_hsu_tx_program_get_default_config(tx_offset);
    sm_config_set_out_shift(&tx, true, false, 32u);
    sm_config_set_out_pins(&tx, PN532_HSU_TX_GPIO, 1u);
    sm_config_set_sideset_pins(&tx, PN532_HSU_TX_GPIO);
    sm_config_set_fifo_join(&tx, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&tx, divider);
    pio_sm_init(PN532_HSU_PIO, PN532_HSU_TX_SM, tx_offset, &tx);
    /* Establish idle high before handing the pin to PIO. */
    pio_sm_set_pins_with_mask(PN532_HSU_PIO,
                              PN532_HSU_TX_SM,
                              1u << PN532_HSU_TX_GPIO,
                              1u << PN532_HSU_TX_GPIO);
    pio_sm_set_consecutive_pindirs(
        PN532_HSU_PIO, PN532_HSU_TX_SM, PN532_HSU_TX_GPIO, 1u, true);
    pio_gpio_init(PN532_HSU_PIO, PN532_HSU_TX_GPIO);

    pio_sm_config rx = pn532_hsu_rx_program_get_default_config(rx_offset);
    sm_config_set_in_pins(&rx, PN532_HSU_RX_GPIO);
    sm_config_set_jmp_pin(&rx, PN532_HSU_RX_GPIO);
    sm_config_set_in_shift(&rx, true, false, 32u);
    sm_config_set_fifo_join(&rx, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&rx, divider);
    pio_sm_init(PN532_HSU_PIO, PN532_HSU_RX_SM, rx_offset, &rx);
    pio_sm_set_consecutive_pindirs(
        PN532_HSU_PIO, PN532_HSU_RX_SM, PN532_HSU_RX_GPIO, 1u, false);
    pio_gpio_init(PN532_HSU_PIO, PN532_HSU_RX_GPIO);
    gpio_pull_up(PN532_HSU_RX_GPIO);
    (void)discard_input();
    pio_sm_set_enabled(PN532_HSU_PIO, PN532_HSU_RX_SM, true);
    pio_sm_set_enabled(PN532_HSU_PIO, PN532_HSU_TX_SM, true);
    configured = true;
    return true;
}

static bool hsu_write(const uint8_t *bytes, size_t size)
{
    if (!configured || !discard_input())
    {
        return false;
    }
    const uint32_t start_us = time_us_32();
    for (size_t index = 0u; index < size; index++)
    {
        while (pio_sm_is_tx_fifo_full(PN532_HSU_PIO, PN532_HSU_TX_SM))
        {
            if ((uint32_t)(time_us_32() - start_us) >= HSU_TIMEOUT_US)
            {
                return false;
            }
        }
        pio_sm_put(PN532_HSU_PIO, PN532_HSU_TX_SM, bytes[index]);
#ifdef PNU_HSU_TRACE
        trace.tx_bytes++;
#endif
    }
    return true;
}

static bool hsu_init(void)
{
    if (initialized)
    {
        return true;
    }
    if (!configure_pio())
    {
        return false;
    }
    /* HSU wakeup preamble followed by a host ACK to abort a stale command. */
    static const uint8_t wake[] = {0x55u, 0x55u, 0u, 0u, 0u};
    if (!hsu_write(wake, sizeof(wake)))
    {
        return false;
    }
    sleep_ms(HSU_WAKEUP_DELAY_MS);
    if (!hsu_write(pn532_ack, sizeof(pn532_ack)))
    {
        return false;
    }
    sleep_ms(PN532_COMMAND_ABORT_DELAY_MS);
    if (!discard_input())
    {
        return false;
    }
    initialized = true;
    return true;
}

static bool hsu_begin_read(void)
{
    read_started_us = time_us_32();
    return initialized;
}

static bool hsu_read(uint8_t *bytes, size_t size)
{
    for (size_t index = 0u; index < size; index++)
    {
        while (pio_sm_is_rx_fifo_empty(PN532_HSU_PIO, PN532_HSU_RX_SM))
        {
            if (receive_failed())
            {
                return false;
            }
        }
        if (receive_failed())
        {
            return false;
        }
        /* Right-shifted samples occupy ISR bits 31..24 after eight bits. */
        bytes[index] = (uint8_t)(pio_sm_get(PN532_HSU_PIO, PN532_HSU_RX_SM) >>
                                 HSU_RX_BYTE_SHIFT);
#ifdef PNU_HSU_TRACE
        if (trace.rx_bytes < sizeof(trace.prefix))
        {
            trace.prefix[trace.rx_bytes] = bytes[index];
        }
        trace.rx_bytes++;
#endif
    }
    return true;
}

static void hsu_end_read(void)
{
    /* HSU is a continuous byte stream; no chip-select transaction to end. */
}

const pn532_transport_ops_t pn532_hsu_ops = {
    .init = hsu_init,
    .write = hsu_write,
    .begin_read = hsu_begin_read,
    .read = hsu_read,
    .end_read = hsu_end_read,
};
