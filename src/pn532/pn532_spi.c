#include "board/board_config.h"
#include "pn532/pn532_frame.h"
#include "pn532/pn532_transport.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pn532_spi.pio.h"

#define SPI_BYTE_TIMEOUT_US 1000u
#define PN532_COMMAND_TIMEOUT_US 100000u
#define SPI_CLOCK_HZ 2000000u
#define SPI_PIO_CYCLES_PER_BIT 4u
#define SPI_CS_SETUP_US 2u
#define SPI_CS_HOLD_US 2u
#define SPI_WAKEUP_CS_MS 2u
#define SPI_READY_POLL_US 100u
#define SPI_OPERATION_WRITE 0x01u
#define SPI_OPERATION_STATUS 0x02u
#define SPI_OPERATION_READ 0x03u
#define SPI_STATUS_READY 0x01u
#define SPI_DUMMY_BYTE 0xffu
#define SPI_RX_BYTE_SHIFT 24u

static bool initialized;

static bool transfer(uint8_t tx, uint8_t *rx)
{
    const uint32_t start = time_us_32();
    while (pio_sm_is_tx_fifo_full(PN532_SPI_PIO, PN532_SPI_SM))
    {
        if ((uint32_t)(time_us_32() - start) >= SPI_BYTE_TIMEOUT_US)
        {
            return false;
        }
    }
    pio_sm_put(PN532_SPI_PIO, PN532_SPI_SM, tx);
    while (pio_sm_is_rx_fifo_empty(PN532_SPI_PIO, PN532_SPI_SM))
    {
        if ((uint32_t)(time_us_32() - start) >= SPI_BYTE_TIMEOUT_US)
        {
            return false;
        }
    }
    *rx =
        (uint8_t)(pio_sm_get(PN532_SPI_PIO, PN532_SPI_SM) >> SPI_RX_BYTE_SHIFT);
    return true;
}

static bool write_bytes(const uint8_t *bytes, size_t count)
{
    uint8_t ignored;
    for (size_t index = 0u; index < count; index++)
    {
        if (!transfer(bytes[index], &ignored))
        {
            return false;
        }
    }
    return true;
}

static bool read_bytes(uint8_t *bytes, size_t count)
{
    for (size_t index = 0u; index < count; index++)
    {
        if (!transfer(SPI_DUMMY_BYTE, &bytes[index]))
        {
            return false;
        }
    }
    return true;
}

static bool begin_packet(uint8_t operation)
{
    uint8_t ignored;
    gpio_put(PN532_SPI_CS_GPIO, false);
    /* PN532 requires setup time between CS assertion and the first clock. */
    busy_wait_us_32(SPI_CS_SETUP_US);
    return transfer(operation, &ignored);
}

static void end_packet(void)
{
    gpio_put(PN532_SPI_CS_GPIO, true);
    busy_wait_us_32(SPI_CS_HOLD_US);
}

static bool wait_ready(void)
{
    const uint32_t start = time_us_32();
    do
    {
        uint8_t status = 0u;
        const bool ok =
            begin_packet(SPI_OPERATION_STATUS) && read_bytes(&status, 1u);
        end_packet();
        if (!ok)
        {
            return false;
        }
        if (status == SPI_STATUS_READY)
        {
            return true;
        }
        if (status != 0u)
        {
            /* A floating MISO line must not be mistaken for READY. */
            return false;
        }
        sleep_us(SPI_READY_POLL_US);
    } while ((uint32_t)(time_us_32() - start) < PN532_COMMAND_TIMEOUT_US);
    return false;
}

static void abort_command(void)
{
    (void)(begin_packet(SPI_OPERATION_WRITE) &&
           write_bytes(pn532_ack, sizeof(pn532_ack)));
    end_packet();
    sleep_ms(PN532_COMMAND_ABORT_DELAY_MS);
}

static bool spi_init(void)
{
    if (initialized)
    {
        return true;
    }
    if (!pio_can_add_program(PN532_SPI_PIO, &pn532_spi_program))
    {
        return false;
    }
    gpio_init(PN532_SPI_CS_GPIO);
    gpio_put(PN532_SPI_CS_GPIO, true);
    gpio_set_dir(PN532_SPI_CS_GPIO, GPIO_OUT);
    gpio_init(PN532_SPI_IRQ_GPIO);
    gpio_set_dir(PN532_SPI_IRQ_GPIO, GPIO_IN);
    gpio_disable_pulls(PN532_SPI_IRQ_GPIO);

    const uint offset = pio_add_program(PN532_SPI_PIO, &pn532_spi_program);
    pio_gpio_init(PN532_SPI_PIO, PN532_SPI_SCK_GPIO);
    pio_gpio_init(PN532_SPI_PIO, PN532_SPI_MOSI_GPIO);
    pio_gpio_init(PN532_SPI_PIO, PN532_SPI_MISO_GPIO);
    pio_sm_set_consecutive_pindirs(
        PN532_SPI_PIO, PN532_SPI_SM, PN532_SPI_SCK_GPIO, 1u, true);
    pio_sm_set_consecutive_pindirs(
        PN532_SPI_PIO, PN532_SPI_SM, PN532_SPI_MOSI_GPIO, 1u, true);
    pio_sm_set_consecutive_pindirs(
        PN532_SPI_PIO, PN532_SPI_SM, PN532_SPI_MISO_GPIO, 1u, false);
    pio_sm_set_pins_with_mask(PN532_SPI_PIO,
                              PN532_SPI_SM,
                              0u,
                              (1u << PN532_SPI_SCK_GPIO) |
                                  (1u << PN532_SPI_MOSI_GPIO));
    pio_sm_config config = pn532_spi_program_get_default_config(offset);
    sm_config_set_out_pins(&config, PN532_SPI_MOSI_GPIO, 1u);
    sm_config_set_in_pins(&config, PN532_SPI_MISO_GPIO);
    sm_config_set_sideset_pins(&config, PN532_SPI_SCK_GPIO);
    /* Native LSB-first SPI; incoming bytes occupy ISR bits 31..24. */
    sm_config_set_out_shift(&config, true, true, 8u);
    sm_config_set_in_shift(&config, true, true, 8u);
    /* Match the proven control bus: each PIO bit takes four cycles. */
    sm_config_set_clkdiv(&config,
                         (float)clock_get_hz(clk_sys) /
                             (float)(SPI_CLOCK_HZ * SPI_PIO_CYCLES_PER_BIT));
    gpio_set_outover(PN532_SPI_SCK_GPIO, GPIO_OVERRIDE_NORMAL);
    /* SPI provides its clock; retain the reference's synchronous input path. */
    hw_set_bits(&PN532_SPI_PIO->input_sync_bypass, 1u << PN532_SPI_MISO_GPIO);
    pio_sm_init(PN532_SPI_PIO, PN532_SPI_SM, offset, &config);
    pio_sm_set_enabled(PN532_SPI_PIO, PN532_SPI_SM, true);
    gpio_put(PN532_SPI_CS_GPIO, false);
    sleep_ms(SPI_WAKEUP_CS_MS);
    end_packet();
    abort_command();
    initialized = true;
    return true;
}

static bool spi_write(const uint8_t *bytes, size_t size)
{
    const bool ok =
        begin_packet(SPI_OPERATION_WRITE) && write_bytes(bytes, size);
    end_packet();
    return ok;
}

static bool spi_begin_read(void)
{
    return wait_ready() && begin_packet(SPI_OPERATION_READ);
}

static bool spi_read(uint8_t *bytes, size_t size)
{
    return read_bytes(bytes, size);
}

static void spi_end_read(void)
{
    end_packet();
}

const pn532_transport_ops_t pn532_spi_ops = {
    .init = spi_init,
    .write = spi_write,
    .begin_read = spi_begin_read,
    .read = spi_read,
    .end_read = spi_end_read,
};
