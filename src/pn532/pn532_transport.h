#pragma once

#include "pn532_unleashed/config.h"
#include "pn532_unleashed/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PN532_COMMAND_ABORT_DELAY_MS 2u

/* One immutable binding per initialization. Both backends use the same PN532
 * command framing; calls are foreground-only. Always end a read after failure
 * so SPI CS is released. */
typedef struct
{
    bool (*init)(void);
    bool (*write)(const uint8_t *bytes, size_t size);
    bool (*begin_read)(void);
    bool (*read)(uint8_t *bytes, size_t size);
    void (*end_read)(void);
} pn532_transport_ops_t;

extern const pn532_transport_ops_t pn532_spi_ops;
extern const pn532_transport_ops_t pn532_hsu_ops;

void pn532_transport_select(nfc_transport_t transport);
bool pn532_transport_init(void);
bool pn532_transport_write(const uint8_t *bytes, size_t size);
bool pn532_transport_begin_read(void);
bool pn532_transport_read(uint8_t *bytes, size_t size);
void pn532_transport_end_read(void);

#ifdef PNU_HSU_TRACE
/* Optional foreground-only HSU diagnostics; bounded text, no live RF logs. */
uint16_t pn532_hsu_trace(char *output, uint16_t capacity);
#endif
