"""Convert the standalone RP2040 application binary into ROM UF2 blocks."""

import argparse
from pathlib import Path
import struct


PAYLOAD_BYTES = 256
FLASH_BASE_ADDRESS = 0x10000000
FLASH_CAPACITY_BYTES = 2 * 1024 * 1024
MAGIC_START_0 = 0x0A324655
MAGIC_START_1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FAMILY_ID_PRESENT = 0x2000
RP2040_FAMILY_ID = 0xE48BFF56
# Eight header words, payload, reserved padding and the trailing magic word.
BLOCK = struct.Struct(f"<8I{PAYLOAD_BYTES}s220xI")


def convert(binary: bytes) -> bytes:
    if not binary or len(binary) > FLASH_CAPACITY_BYTES:
        raise ValueError("Expected a nonempty binary fitting the 2 MiB flash")
    count = (len(binary) + PAYLOAD_BYTES - 1) // PAYLOAD_BYTES
    blocks = []
    for index in range(count):
        payload = binary[
            index * PAYLOAD_BYTES : (index + 1) * PAYLOAD_BYTES
        ].ljust(PAYLOAD_BYTES, b"\0")
        blocks.append(
            BLOCK.pack(
                MAGIC_START_0,
                MAGIC_START_1,
                FAMILY_ID_PRESENT,
                FLASH_BASE_ADDRESS + index * PAYLOAD_BYTES,
                PAYLOAD_BYTES,
                index,
                count,
                RP2040_FAMILY_ID,
                payload,
                MAGIC_END,
            )
        )
    return b"".join(blocks)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(convert(args.binary.read_bytes()))


if __name__ == "__main__":
    main()
