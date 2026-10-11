#include "pn532/pn532_transport.h"

#include <assert.h>
#include <string.h>

static unsigned int spi_calls;
static unsigned int hsu_calls;

static bool spi_init(void)
{
    spi_calls++;
    return true;
}

static bool hsu_init(void)
{
    hsu_calls++;
    return true;
}

static bool spi_write(const uint8_t *bytes, size_t size)
{
    spi_calls++;
    return size == 1u && bytes[0] == 0x55u;
}

static bool hsu_write(const uint8_t *bytes, size_t size)
{
    hsu_calls++;
    return size == 1u && bytes[0] == 0x55u;
}

static bool spi_begin_read(void)
{
    spi_calls++;
    return true;
}

static bool hsu_begin_read(void)
{
    hsu_calls++;
    return true;
}

static bool spi_read(uint8_t *bytes, size_t size)
{
    spi_calls++;
    bytes[0] = 0x11u;
    return size == 1u;
}

static bool hsu_read(uint8_t *bytes, size_t size)
{
    hsu_calls++;
    bytes[0] = 0x22u;
    return size == 1u;
}

static void spi_end_read(void)
{
    spi_calls++;
}

static void hsu_end_read(void)
{
    hsu_calls++;
}

const pn532_transport_ops_t pn532_spi_ops = {
    spi_init, spi_write, spi_begin_read, spi_read, spi_end_read};
const pn532_transport_ops_t pn532_hsu_ops = {
    hsu_init, hsu_write, hsu_begin_read, hsu_read, hsu_end_read};

int main(void)
{
    uint8_t byte = 0x55u;
    assert(!pn532_transport_init());
    assert(strcmp(nfc_transport_name(), "unselected") == 0);
    pn532_transport_select(NFC_TRANSPORT_HSU);
    assert(strcmp(nfc_transport_name(), "HSU") == 0);
    assert(pn532_transport_init());
    assert(pn532_transport_write(&byte, 1u));
    assert(pn532_transport_begin_read());
    assert(pn532_transport_read(&byte, 1u) && byte == 0x22u);
    pn532_transport_end_read();
    assert(hsu_calls == 5u && spi_calls == 0u);
    pn532_transport_select(NFC_TRANSPORT_SPI);
    assert(strcmp(nfc_transport_name(), "SPI") == 0);
    byte = 0x55u;
    assert(pn532_transport_init());
    assert(pn532_transport_write(&byte, 1u));
    assert(pn532_transport_begin_read());
    assert(pn532_transport_read(&byte, 1u) && byte == 0x11u);
    pn532_transport_end_read();
    assert(hsu_calls == 5u && spi_calls == 5u);
    return 0;
}
