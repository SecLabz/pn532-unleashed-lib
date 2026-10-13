# Agent instructions

## Purpose and scope

pn532-unleashed is a minimal RP2040 library for direct RF operation with a
PN532. The RP2040 Zero USB firmware in the separate pn532-unleashed repository
is its reference application.
This repository owns the C library, standalone examples and autonomous tests.
Host control, flash slots, Flipper conversion and host solvers belong to
the reference repository.

The initial scope is:
- NFC-A reader: NTAG213/215/216, Ultralight/EV1 and Classic with supplied keys.
- NFC-A user-data restoration for those memory models, with expected-target
  identity, target credential/permission preflight and verified one-shot writes.
  Preserve manufacturer/UID, Classic trailers and Type 2 OTP/lock/config pages.
- MIFARE Classic key recovery: dictionaries, Nested, Hardnested and Darkside,
  orchestrated by the Rust pnu CLI with bounded direct-RF acquisition.
- MIFARE Classic magic-card generation and UID-change signature detection.
  Explicit magic-card UID writes use generation-specific paths with
  expected-identity validation and post-write verification; detection probes
  send no write data.
- Passive NFC-A 106 kbit/s dual sniffing with optional AUX1/AUX2 wiring,
  bounded capture buffers and live CLI command annotations.
- Passive dual NFC-V sniffing: reader 1-of-4/1-of-256 and high-rate
  single-subcarrier tag responses, with optional AUX1 wiring.
- NFC-V reader: inventory, system information, complete readable memory dump.
- NFC-V tag emulation: generic ISO15693, SLIX and SLIX-L.
- NFC-A memory emulation: Classic Mini/1K/2K/4K, NTAG213/215/216,
  Ultralight and Ultralight EV1 48/128.
- NFC-A activation/transport and public discovery: match the limited ISO-DEP,
  ISO4, DESFire, ST25TA and MIFARE Plus profiles documented by this library.
  No authenticated DESFire files or MIFARE Plus secure messaging.
- Faithful emulation of a supplied NTAG215 Amiibo dump.
- Loading and exporting supported Flipper Zero .nfc files through the Rust pnu CLI.
- PN532 configuration over SPI or HSU, selected at library initialization.
- Host control over USB CDC.
- One active RF mode and one loaded card image in RAM.
- Named tag storage in the reference application's RP2040 flash (32 slots).



## Minimalism is a requirement

Implement the smallest complete solution for the agreed scope.
- Prefer explicit states, direct calls, and small protocol-specific modules.
- Introduce abstractions only when they reduce actual duplication or establish
  a necessary hardware or test boundary.
- Share PN532 command framing and RF configuration between SPI and HSU.
- Keep a single physical RF implementation regardless of PN532 control transport.
- Do not introduce a universal hardware abstraction layer, plugin framework,
  dynamic registry, generic scheduler, or compatibility layer for future use.
- Keep one library, one reference firmware application, and one small Rust
  command-line package.
- Import only code required by the supported features; remove unused paths.
- Prefer static allocation and bounded buffers in firmware.
- Keep production C, Rust and Python files below 500 lines unless there is a clear,
  documented reason to split differently.
- Keep dependencies and build options limited to real needs.
- Do not create empty directory hierarchies or speculative APIs.

## Language and licensing

All code identifiers, comments, documentation, CLI help, and runtime messages
must be in English.

The project license is GPL-3.0-or-later. Preserve applicable
third-party copyright and license notices when importing their code, and
record only dependencies actually used in docs/licenses/NOTICE.md.

## Firmware coding style

Use portable C11 and clang-format 17 with the root .clang-format.
- Four-space indentation, Allman braces, and an 80-column target.
- Always use braces for control statements.
- Use snake_case for functions and variables, and upper-case macro names.
- Use nfca and nfcv at public protocol boundaries.
- Use domain names rather than pnu_/PNU_ prefixes in C APIs; use nfc_
  for shared lifecycle, configuration and status. Keep PNU_ build options.
- Name RX and TX from the module owner's perspective; use pcd and picc where
  direction would otherwise be ambiguous.
- Put physical units in names: _us, _cycles, _fc, _bits, _bytes, and _slots.
- Prefer guard clauses and explicit status values: reject invalid arguments,
  state and failed operations before the normal path. Do not wrap the normal
  path in a success-only if or retain an else after a terminating return.
- Preserve validation order, required cleanup and protocol state transitions
  when flattening control flow. Use continue for skipped loop entries and
  keep cleanup outside guarded operation blocks when it must always run.
- Keep public headers small and document ownership, units, capacities,
  lifetimes, output sizes, and failure behavior.
- Group related mutable state into an explicit context with a complete reset.
- Explain invariants and timing decisions rather than restating the code.
- Keep functions linear and bounded.
- Keep formatting, file moves, timing changes, and behavior changes separate
  when practical.

## Ownership and directory boundaries

- src/board: pin assignments, clock, and board initialization.
- src/pn532: SPI/HSU transport, PN532 framing, and RF configuration.
- src/phy: deterministic NFC-A/NFC-V capture and transmission, PIO.
- src/protocols: activation and protocol state machines.
- src/cards: card memory, access rules, and card-specific commands.
- src/cards/type2: shared NTAG and Ultralight/EV1 memory; reader commands in reader/.
- src/emulation: RF role orchestration, independent of host control.
- src/core.c: lifecycle and the single foreground RF ownership state.
- tests: meaningful host-side checks and documented hardware procedures.
- docs: wiring, architecture and public API guides.

The firmware receives a validated, compact binary image. It does not parse
Flipper text files. The Rust CLI validates supported model, UID, memory
geometry, required metadata, and required authentication material. Reject
unsupported or incomplete inputs with a precise reason.

Validate a complete upload before starting emulation. Do not expose a partial
image to the RF engine. Serial traffic must never be required to answer a
reader command.

All tag storage commands require RF idle. Reserve the last 384 KiB of the
reference board's 2 MiB flash; protect the boundary at link time. Use fixed
slots, validate the complete image, and publish the header last. Refuse duplicate
names. No filesystem, automatic persistence of emulated writes, or boot autorun.
Flash mutation must disable local interrupts through Pico SDK safe execution;
core 1 is dormant in this reference application.

## Physical RF and timing rules

SIGOUT feeds the RP2040 receiver and SIGIN carries its transmit waveform.
SPI/HSU configures the PN532 outside timing-critical RF paths.

- Keep protocol decoders explicit and physical; do not add phase search,
  command templates, frame scoring, or heuristic decoder fallbacks.
- Preserve the verified capture grids, polarity, EOF rules, FDT, and pulse shape.
- Disable RX while handling tag commands and during tag load-modulation TX.
- Keep critical timing routines short and in RAM where required.
- Do not allocate, log, parse host commands, or perform SPI/HSU transactions
  inside RF IRQs or timing-critical paths.
- For user-requested bench diagnosis, the optional PNU_RF_TRACE build may
  retain bounded RAM records after response transmission and before RX rearm.
  Never stream them during RF. Keep the normal build free of these hooks;
  diagnostic timing experiments do not qualify production settings.
- A reader transaction may continue only after a valid physical response.
- Mode changes must stop capture and transmission before reconfiguring RF.
- Startup, errors, and stop operations must leave the RF outputs in a
  documented inactive state.

## Verification and hardware handling

Use focused host tests for Flipper parsing, wire framing, malformed inputs,
upload validation, and card behavior. Build both PN532 control transports once
implemented. Do not write tests that only repeat the implementation.

Changes to GPIO, PIO, clocks, FDT, or modulation require physical regression
tests for the affected models. Distinguish built, host-tested, and on-air
validated features in documentation.

Run one Proxmark3 client at a time. Do not capture RP2040 serial logs during
RF reliability measurements. Store local captures and logs under .cache and
never commit them.

Keep USB startup and recovery bounded and test recovery deliberately.
Do not repeatedly flash when USB enumeration or access is blocked;
diagnose host access first.

## Repository boundaries

`include/` contains the public API, including receiver settings and optional
bench diagnostics. `src/` is private; do not expose its include paths to
consumers. All consumers link the single public library target. Do not introduce
a runtime service, USB protocol or Rust package into this repository.

Standalone tests must run without the reference repository or Rust CLI.
Image/Flipper integration tests remain with the reference application and
must continue using its Rust parser. Do not add a second production parser.
