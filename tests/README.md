# Standalone library checks

Run from the library root, without a USB device:

```sh
./tests/run_host.sh
CLANG_FORMAT=clang-format-17 ./tests/check_style.sh
```

Native tests need Python's standard library and a C compiler.
Style checks need clang-format 17 and Black 24.8.0.

## Coverage

Checks exercise lifecycle/resource conflicts, SPI/HSU dispatch, public
activation, ISO-DEP failures, typed card state, snapshot export,
custom handlers, Classic authentication/access rules, Type 2 passwords/
counters/locks, ISO4 replies, NFC-V IRQ dispatch, memory and SLIX passwords.
Physical grid and recovery-clock checks use substitutes; they do not measure
RF reliability.
Pico SDK stubs live in `stubs/`; compiled helpers stay under `.cache/`.

Type 2 expectations follow NXP lock diagrams/command tables, independently
of the production model table. They cover L/BL groups, direct/compatibility
writes, imported/simultaneous locks and deferred CFGLCK.
[Field-loss and model limits →](../docs/emulation.md#type-2-behavior)

Frozen [synthetic image fixtures](fixtures/images/README.md) allow tests to
run independently. Mutations call the production C validator; the tests do
not implement another production image parser. The Classic peer uses the
library's Crypto1 primitives and does not qualify independent cryptography.

## ARM consumers

Build independent C++ consumers with SPI/HSU, normal, RF-only and HSU-only
trace configurations:

```sh
PICO_SDK_PATH=/path/to/pico-sdk ./tests/build_consumers.sh
```

Also build the six [C examples](../examples/README.md).
[CI](../.github/workflows/checks.yml) pins the SDK and formatter versions.
