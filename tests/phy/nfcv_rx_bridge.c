/* Exercise production IRQ dispatch and decoder; no analog/timing model. */
#include "board/tag_board.h"
#include "phy/nfcv/picc/nfcv_rx.h"
#include "phy/picc/picc_phy.h"

#include <assert.h>
#include <string.h>

pio_hw_t host_rx_pio;
board_config_t board_config;
static irq_handler_t rx_irq;
static bool irq_enabled, capture_enabled, in_irq, in_handler;
static uint8_t fifo[8];
static uint16_t fifo_size, fifo_offset;
static uint32_t callbacks, now_us, received_eof_us, rearms;
static nfcv_frame_t received;

uint32_t time_us_32(void)
{
    return now_us;
}
void irq_set_enabled(uint irq, bool enabled)
{
    assert(irq == TAG_RX_IRQ);
    assert(!enabled || !in_handler);
    irq_enabled = enabled;
}
void irq_clear(uint irq)
{
    assert(irq == TAG_RX_IRQ);
}
void irq_set_exclusive_handler(uint irq, irq_handler_t handler)
{
    assert(irq == TAG_RX_IRQ && rx_irq == NULL);
    rx_irq = handler;
}
void irq_remove_handler(uint irq, irq_handler_t handler)
{
    assert(irq == TAG_RX_IRQ && rx_irq == handler && !irq_enabled);
    rx_irq = NULL;
}
void irq_set_priority(uint irq, uint priority)
{
    assert(irq == TAG_RX_IRQ && priority == PICO_HIGHEST_IRQ_PRIORITY);
}
bool pio_can_add_program(PIO pio, const struct pio_program *program)
{
    assert(pio == TAG_RX_PIO && program != NULL);
    return true;
}
uint pio_add_program(PIO pio, const struct pio_program *program)
{
    assert(pio == TAG_RX_PIO && program != NULL);
    return 0u;
}
void pio_remove_program(PIO pio, const struct pio_program *program, uint offset)
{
    assert(pio == TAG_RX_PIO && program != NULL && offset == 0u);
}
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled)
{
    assert(pio == TAG_RX_PIO && sm == TAG_RX_SM);
    capture_enabled = enabled;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm)
{
    assert(pio == TAG_RX_PIO && sm == TAG_RX_SM);
    return fifo_offset == fifo_size;
}
uint32_t pio_sm_get(PIO pio, uint sm)
{
    assert(!pio_sm_is_rx_fifo_empty(pio, sm));
    return fifo[fifo_offset++];
}
void sm_config_set_clkdiv(pio_sm_config *config, float divider)
{
    assert(config != NULL && divider == 5.0f);
}
void sm_config_set_jmp_pin(pio_sm_config *config, uint pin)
{
    assert(config != NULL && pin == TAG_RX_GPIO);
}
void sm_config_set_in_pins(pio_sm_config *config, uint pin)
{
    assert(config != NULL && pin == TAG_RX_GPIO);
}
void sm_config_set_in_shift(pio_sm_config *config,
                            bool right,
                            bool push,
                            uint threshold)
{
    assert(config != NULL && !right && push && threshold == 8u);
}
void sm_config_set_fifo_join(pio_sm_config *config, uint join)
{
    assert(config != NULL && join == PIO_FIFO_JOIN_RX);
}
void pio_sm_init(PIO pio, uint sm, uint offset, const pio_sm_config *config)
{
    assert(pio == TAG_RX_PIO && sm == TAG_RX_SM && offset == 0u);
    assert(config != NULL);
}
void tag_rx_pio_prepare_input(void)
{
}
void tag_rx_pio_stop(void)
{
    capture_enabled = false;
}
void tag_rx_pio_reset(uint32_t offset)
{
    assert(offset == 0u && !in_handler);
    capture_enabled = false;
    fifo_size = fifo_offset = 0u;
}
void tag_rx_pio_rearm(uint32_t offset)
{
    tag_rx_pio_reset(offset);
    capture_enabled = true;
    rearms++;
}
void tag_rx_pio_fifo_irq_set_enabled(bool enabled)
{
    (void)enabled;
}
static void handle_frame(const nfcv_frame_t *frame, uint32_t eof_time_us)
{
    /* The caller must complete handling/TX before rearming capture. */
    assert(in_irq && !irq_enabled && !capture_enabled && !in_handler);
    in_handler = true;
    received = *frame;
    received_eof_us = eof_time_us;
    callbacks++;
    in_handler = false;
}
void host_nfcv_start(void)
{
    if (rx_irq != NULL)
    {
        nfcv_rx_deinit();
    }
    callbacks = received_eof_us = rearms = 0u;
    now_us = 123456u;
    memset(&received, 0, sizeof(received));
    assert(nfcv_rx_init(handle_frame));
}
void host_nfcv_samples(const uint8_t *samples, uint16_t size_bytes)
{
    assert(size_bytes <= sizeof(fifo));
    assert(rx_irq != NULL && irq_enabled && capture_enabled);
    memcpy(fifo, samples, size_bytes);
    fifo_size = size_bytes;
    fifo_offset = 0u;
    in_irq = true;
    rx_irq();
    in_irq = false;
    assert(fifo_offset == fifo_size && irq_enabled && capture_enabled);
}
uint32_t host_nfcv_callbacks(void)
{
    return callbacks;
}
uint32_t host_nfcv_eof_us(void)
{
    return received_eof_us;
}
uint32_t host_nfcv_rearms(void)
{
    return rearms;
}
uint16_t host_nfcv_frame(uint8_t *output)
{
    assert(received.crc_ok && received.high_data_rate &&
           received.single_subcarrier);
    memcpy(output, received.bytes, received.size_bytes);
    return received.size_bytes;
}
void host_nfcv_stop(void)
{
    nfcv_rx_deinit();
    assert(rx_irq == NULL && !irq_enabled && !capture_enabled);
}
