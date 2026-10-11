#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared mechanical lifecycle of the tag RX PIO state machine. */
void tag_rx_pio_prepare_input(void);
void tag_rx_pio_stop(void);
void tag_rx_pio_start(void);
void tag_rx_pio_stop_and_drain(void);
void tag_rx_pio_reset(uint32_t program_offset);
void tag_rx_pio_rearm(uint32_t program_offset);
void tag_rx_pio_fifo_irq_set_enabled(bool enabled);

/*
 * Initializes the NFC-A tag load-modulation GPIO and response timer.
 */
bool tag_phy_init(void);

/*
 * Initializes direct-SIO load modulation and the response timer without
 * assigning the load pin to the NFC-A PIO streamer.
 */
void tag_phy_init_sio(void);

/*
 * Stops tag timing and forces load modulation to its inactive level.
 */
void tag_phy_stop(void);
