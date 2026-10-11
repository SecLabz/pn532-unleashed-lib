#pragma once

#include <stdbool.h>

#define PIO0_IRQ_0 7u
#define PICO_HIGHEST_IRQ_PRIORITY 0u
typedef void (*irq_handler_t)(void);

bool irq_has_shared_handler(unsigned int irq);
irq_handler_t irq_get_exclusive_handler(unsigned int irq);
void irq_set_enabled(unsigned int irq, bool enabled);
void irq_clear(unsigned int irq);
void irq_set_exclusive_handler(unsigned int irq, irq_handler_t handler);
void irq_set_priority(unsigned int irq, unsigned int priority);
void irq_remove_handler(unsigned int irq, irq_handler_t handler);
