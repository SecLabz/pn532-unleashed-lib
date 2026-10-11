#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;
struct pio_program
{
    uint8_t length;
};

typedef void *PIO;
#define pio0 ((PIO)0)
#define pio1 ((PIO)1)

bool pio_sm_is_claimed(PIO pio, unsigned int sm);
void pio_sm_claim(PIO pio, unsigned int sm);
bool pio_can_add_program(PIO pio, const struct pio_program *program);
uint pio_add_program(PIO pio, const struct pio_program *program);
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm);
void pio_sm_put(PIO pio, uint sm, uint32_t data);
