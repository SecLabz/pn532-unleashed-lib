# Add the library to your project

A static C11 Pico SDK library for RP2040, also callable from C++.
Use public headers and link `pn532_unleashed::pn532_unleashed`.

SPI/HSU configures the PN532; RP2040 PIO handles RF through SIGIN/SIGOUT.

## Build

Place the library in `pn532-unleashed-lib/` beside your `CMakeLists.txt`
and `main.c`. Install CMake, Ninja, Arm bare-metal GCC and Pico SDK with its
submodules (checked: 2.1.0). Set `PICO_SDK_PATH` to the SDK checkout.

```cmake
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD waveshare_rp2040_zero)
set(PICO_STACK_SIZE 4096)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(my_nfc_tool C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(pn532-unleashed-lib)
add_executable(my_nfc_tool main.c)
target_link_libraries(my_nfc_tool PRIVATE
    pn532_unleashed::pn532_unleashed)
pico_add_extra_outputs(my_nfc_tool)
```

Run `cmake -S . -B build -G Ninja`, then `cmake --build build`.

## Initialize and read

Copy this as `main.c`. With [default SPI wiring](hardware.md), it reads an
unprotected NTAG213 once. Inspect the static dump in a debugger or add output
after RF stops. Change `config.transport` for HSU.

```c
#include "pn532_unleashed.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

int main(void)
{
    set_sys_clock_khz(PNU_REQUIRED_SYS_CLOCK_HZ / 1000u, true);
    nfc_config_t config = nfc_config_default();
    config.transport = NFC_TRANSPORT_SPI; /* Change to NFC_TRANSPORT_HSU. */
    if (nfc_init(&config) != NFC_OK)
    {
        (void)nfc_stop();
        return 1;
    }

    static nfca_dump_t dump;
    nfca_tag_t tag;
    nfc_status_t status = nfca_scan(&tag);
    if (status == NFC_OK)
    {
        status = nfca_select(&tag, CARD_NTAG213);
        if (status == NFC_OK)
        {
            status = nfca_read_all(NULL, &dump);
        }
    }
    const nfc_status_t cleanup = nfc_stop();
    return status == NFC_OK && cleanup == NFC_OK ? 0 : 1;
}
```

Set the clock before init: default 135.6 MHz. Both control backends are built
in. Pins/transport become fixed once resources are claimed, until MCU reset.

## Use one RF role

Call from foreground code on one core, without concurrent/reentrant calls.
Init enters idle. NFC-V complete reads release RF. Raw readers, selected NFC-A access and
emulation keep it until stop. Competing operations return `NFC_BUSY`.

NFC-A and NFC-V emulation respond in RF interrupts.
[Emulation API →](emulation.md)

Stop halts capture/TX, disconnects SIGIN and requests field-off/Idle.
Repeated successful stops are safe. Failed cleanup returns `NFC_RF_ERROR`;
cleanup must succeed before another role. Stop failed custom readers before
retrying init. [Lifecycle contract →](../include/pn532_unleashed/core.h)

## `nfc_poll()`

Update the activity LED. Call regularly from the main loop when enabled.

Related guides:

- [Pin configuration](hardware.md#change-pins) and
  [reserved resources](hardware.md#reserved-resources).
- Raw reader exchanges: [NFC-A](nfca.md#raw-reader-exchanges) and
  [NFC-V](nfcv.md#raw-reader-exchanges).
- [Type 2 locks, counters and CFGLCK](emulation.md#type-2-behavior).
