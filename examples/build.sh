#!/bin/sh
set -eu

usage()
{
    cat <<'EOF'
Usage: ./examples/build.sh [example|all] [SPI|HSU]

Defaults: all examples, SPI transport.
Examples: nfca_reader, slix_reader, mifare_classic_emulator,
          ntag_emulator, slix_emulator, ntag_custom_handler.
Set PICO_SDK_PATH to an installed Pico SDK with its submodules.
EOF
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi
if [ "$#" -gt 2 ]; then
    usage >&2
    exit 2
fi

example=${1:-all}
transport=${2:-SPI}
case "$example" in
    all|nfca_reader|slix_reader|mifare_classic_emulator|ntag_emulator|\
        slix_emulator|ntag_custom_handler) ;;
    *)
        printf 'Error: unknown example "%s".\n' "$example" >&2
        usage >&2
        exit 2
        ;;
esac
case "$transport" in
    SPI|HSU) ;;
    *)
        printf 'Error: transport must be SPI or HSU.\n' >&2
        exit 2
        ;;
esac

if [ ! -f "${PICO_SDK_PATH:-}/external/pico_sdk_import.cmake" ]; then
    printf 'Error: set PICO_SDK_PATH to an installed Pico SDK.\n' >&2
    exit 1
fi
for tool in cmake ninja arm-none-eabi-gcc arm-none-eabi-g++ python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Error: required tool "%s" is missing from PATH.\n' "$tool" >&2
        exit 1
    fi
done

library_dir=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
sdk_dir=$(CDPATH= cd "$PICO_SDK_PATH" && pwd)
build_dir="$library_dir/build/examples/$transport"

cmake -S "$library_dir/examples" -B "$build_dir" -G Ninja \
    -DPICO_SDK_PATH="$sdk_dir" -DPNU_TRANSPORT="$transport" \
    -DPICO_NO_PICOTOOL=ON
cmake --build "$build_dir" --target "$example" --parallel
printf '\nBuild complete. UF2 files: %s/*.uf2\n' "$build_dir"
