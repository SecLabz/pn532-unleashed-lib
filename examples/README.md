# Try an example

Complete RP2040 Zero applications use only public headers and the CMake
library target. Copy a `main.c`, adjust its data and run one example at a time.

| Example | What to adjust |
| --- | --- |
| [NFC-A reader](nfca_reader/README.md) | Model and supplied credentials |
| [SLIX reader](slix_reader/README.md) | Repeated NFC-V memory/security reads |
| [NFC-A sniffer](nfca_sniffer/README.md) | AUX1/AUX2 pins and capture duration |
| [NFC-V sniffer](nfcv_sniffer/README.md) | AUX1 pin and capture duration |
| [SLIX Emulator](slix_emulator/README.md) | UID, four-byte blocks and lock state |
| [MIFARE Classic Emulator](mifare_classic_emulator/README.md) | One-block-per-row memory image |
| [NTAG Emulator](ntag_emulator/README.md) | One-page-per-row memory and tag state |
| [NTAG Custom Handler](ntag_custom_handler/README.md) | Read-only memory and card commands |

## Build

Install CMake, Ninja, Arm bare-metal GCC, Python 3.10+ and Pico SDK with its
submodules (checked: SDK 2.1.0). Follow the [wiring guide](../docs/hardware.md).
On macOS or Linux, run from the library root:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
./examples/build.sh                    # All examples, SPI
./examples/build.sh ntag_emulator       # One example, SPI
./examples/build.sh slix_emulator HSU   # One example, HSU
```

Use any example name from the table, or `all HSU` to build all with HSU.
Outputs are in `build/examples/SPI/` or `build/examples/HSU/`:
`.elf`, `.bin` and `.uf2`.
Default clock: 135.6 MHz. Both control backends are in the archive; each example
chooses its transport at initialization.

## Load and run

Put the RP2040 in BOOTSEL mode and copy the chosen `.uf2` to its USB drive.

Reader and sniffer output occurs after RF stops. Emulation handlers never print.
Sniffer examples call `nfc_poll()` continuously in their capture loop.
Other examples use it for activity LED updates when enabled. Avoid serial logs
during RF reliability measurements.
