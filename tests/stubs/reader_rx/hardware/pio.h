#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;
typedef struct
{
    uint32_t fdebug;
} pio_hw_t;
typedef pio_hw_t *PIO;
extern pio_hw_t host_rx_pio;
#define pio0 (&host_rx_pio)
#define pio1 (&host_rx_pio)
#define PIO_FIFO_JOIN_RX 1u
#define PIO_FDEBUG_RXSTALL_LSB 0u
struct pio_program
{
    uint dummy;
};
typedef struct
{
    uint dummy;
} pio_sm_config;

void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_clear_fifos(PIO pio, uint sm);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_clkdiv_restart(PIO pio, uint sm);
void pio_sm_exec(PIO pio, uint sm, uint instruction);
uint pio_encode_jmp(uint offset);
bool pio_can_add_program(PIO pio, const struct pio_program *program);
uint pio_add_program(PIO pio, const struct pio_program *program);
void pio_remove_program(PIO pio,
                        const struct pio_program *program,
                        uint offset);
void sm_config_set_clkdiv(pio_sm_config *config, float divider);
void sm_config_set_in_pins(pio_sm_config *config, uint pin);
void sm_config_set_jmp_pin(pio_sm_config *config, uint pin);
void sm_config_set_in_shift(pio_sm_config *config,
                            bool right,
                            bool push,
                            uint threshold);
void sm_config_set_fifo_join(pio_sm_config *config, uint join);
void pio_sm_init(PIO pio, uint sm, uint offset, const pio_sm_config *config);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint32_t pio_sm_get(PIO pio, uint sm);
