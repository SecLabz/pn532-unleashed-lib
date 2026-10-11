# MIFARE Classic Emulator

Start a complete synthetic Classic 1K image.
[Source to copy →](main.c) · [Build target `mifare_classic_emulator` →](../README.md)

| Demo field | Value |
| --- | --- |
| UID | `DEADBEEF` |
| Every sector's Key A / B | `FFFFFFFFFFFF` |
| Access bytes / general-purpose byte | `FF 07 80` / `69` |
| Data byte at offset n | n & 0xff, except block 0 and trailers |

Edit `memory[64][16]`: one row per block, every fourth a trailer.
Keep block-0 UID/BCC/SAK/ATQA consistent. Start validates/copies the image;
reader writes change backend RAM. Reset reloads the original demo.

[Emulation API →](../../docs/emulation.md)
