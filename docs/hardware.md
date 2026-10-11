# Connect RP2040 and PN532

Supported setup: RP2040 Zero + PN532. SPI/HSU configures the chip;
**SIGOUT/SIGIN carry RF**. Control-bus wiring alone is insufficient.
Direct pins may need soldering to test pads or the IC.

Use common ground and the module's documented power input. Match its interface
selector to your configuration, then restart the PN532 itself.

## Tested modules and antenna tuning

All physical RF tests for this project used clones of the Waveshare PN532
module. Two different batches were tested, with substantial differences in
RF performance between them.

Modules marked **`NFC MODULE V3`** performed better than those from the other
batch. NanoVNA measurements showed a clear difference in antenna tuning
between the two batches. These observations concern the tested modules.

Record the batch and PCB marking with RF results, alongside antenna placement
and reader settings.

## Default pins

Both transports need:

| RP2040 GPIO | Direction | PN532 |
| --- | --- | --- |
| 28 | Input | SIGOUT, IC pin 35 |
| 29 | Output | SIGIN, IC pin 36 |
| GND | Common reference | GND |

Add one control transport:

| RP2040 GPIO | SPI | HSU |
| --- | --- | --- |
| 8 | SCK | Unused |
| 9 | MISO | Unused |
| 10 | MOSI | TXD → RP2040 RX |
| 11 | CS / SS | RXD ← RP2040 TX |
| 14 | IRQ | Unused |

SPI is PIO-generated, LSB-first, 2 MHz. HSU is 115200 baud, 8N1, no flow control.
The bench uses no external SVDD; signal voltage/current and analog margins
are uncharacterized. New wiring needs physical checks. Keep SIGIN an input
without pulls before init.

## Change pins

These snippets use the examples' default GPIOs. Change the values to match
your wiring. Call after setting the required clock, before first init.

**SPI:**

```c
#include "pn532_unleashed.h"

nfc_status_t initialize_spi_pins(void)
{
    nfc_config_t config = nfc_config_default();
    config.transport = NFC_TRANSPORT_SPI;
    config.sigout_gpio = 28;
    config.sigin_gpio = 29;
    config.spi.sck_gpio = 8;
    config.spi.miso_gpio = 9;
    config.spi.mosi_gpio = 10;
    config.spi.cs_gpio = 11;
    config.spi.irq_gpio = 14;
    return nfc_init(&config);
}
```

**HSU:**

```c
#include "pn532_unleashed.h"

nfc_status_t initialize_hsu_pins(void)
{
    nfc_config_t config = nfc_config_default();
    config.transport = NFC_TRANSPORT_HSU;
    config.sigout_gpio = 28;
    config.sigin_gpio = 29;
    config.hsu.tx_gpio = 11;
    config.hsu.rx_gpio = 10;
    return nfc_init(&config);
}
```

Active GPIOs must be distinct, available and in 0..29. Only selected-transport
pins are checked. Once resources are claimed, pins/transport are fixed until reset.

## Reserved resources

| Resource | Use |
| --- | --- |
| PIO0 SM0 / SM1, IRQ0 | RF capture/TX, highest-priority handler |
| PWM0, divider one | RF timing |
| One DMA channel | Claimed on first reader use |
| PIO1 SM0 | SPI or HSU TX |
| PIO1 SM1 | HSU RX |
| GPIO16 + PIO1 SM2 | Optional activity LED |

Claims persist until reset; no deinit. Occupied SM/IRQ/active PWM0 returns
`NFC_RESOURCE_BUSY`. GPIO ownership/inactive PWM0 reservations are caller-owned.
Insufficient PIO memory/DMA returns `NFC_RF_ERROR` with cleanup.
Reserve 4 KiB stack; keep large buffers static. Do not replace owned IRQ/PIO
resources or change clocks. [Lifecycle →](library.md)

## Build options

| CMake option | Default | Purpose |
| --- | --- | --- |
| `PNU_SYS_CLOCK_HZ` | `135600000` | Required clock; `54240000` experimental |
| `PNU_ACTIVITY_LED` | `OFF` | RP2040 Zero WS2812 pulses |
| `PNU_RF_TRACE` | `OFF` | Bounded RF bench records in RAM |
| `PNU_HSU_TRACE` | `OFF` | Independent cached HSU diagnostics |

Set the selected clock before init; others return `NFC_WRONG_CLOCK`.
Trace builds expose [`diagnostics.h`](../include/pn532_unleashed/diagnostics.h).
Read cached traces after RF stops.
