#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to an installed Raspberry Pi Pico SDK}"
for transport in SPI HSU; do
    for trace in normal rf hsu; do
        rf=OFF
        hsu=OFF
        if [ "$trace" = rf ]; then rf=ON; fi
        if [ "$trace" = hsu ]; then hsu=ON; fi
        output=".cache/library-consumer/$transport-$trace"
        cmake -S tests/integration -B "$output" -G Ninja \
            -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPNU_TRANSPORT="$transport" \
            -DPICO_NO_PICOTOOL=1 -DPNU_RF_TRACE="$rf" -DPNU_HSU_TRACE="$hsu"
        cmake --build "$output"
    done
done
