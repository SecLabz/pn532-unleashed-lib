#include "pn532/pn532_transport.h"

static const pn532_transport_ops_t *active;

void pn532_transport_select(nfc_transport_t transport)
{
    /* nfc_init validates and fixes the selection before any PN532 command. */
    active = transport == NFC_TRANSPORT_HSU ? &pn532_hsu_ops : &pn532_spi_ops;
}

const char *nfc_transport_name(void)
{
    if (active == &pn532_spi_ops)
    {
        return "SPI";
    }
    if (active == &pn532_hsu_ops)
    {
        return "HSU";
    }
    return "unselected";
}

bool pn532_transport_init(void)
{
    return active != NULL && active->init();
}

bool pn532_transport_write(const uint8_t *bytes, size_t size)
{
    return active != NULL && active->write(bytes, size);
}

bool pn532_transport_begin_read(void)
{
    return active != NULL && active->begin_read();
}

bool pn532_transport_read(uint8_t *bytes, size_t size)
{
    return active != NULL && active->read(bytes, size);
}

void pn532_transport_end_read(void)
{
    if (active != NULL)
    {
        active->end_read();
    }
}

#ifdef PNU_HSU_TRACE
uint16_t nfc_trace_transport(char *output, uint16_t capacity)
{
    return active == &pn532_hsu_ops ? pn532_hsu_trace(output, capacity) : 0u;
}
#endif
