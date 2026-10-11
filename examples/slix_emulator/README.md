# SLIX Emulator

Start a synthetic ICODE SLIX tag using the library's built-in NFC-V behavior.
[Source to copy →](main.c) · [Build target `slix_emulator` →](../README.md)

| Demo field | Value |
| --- | --- |
| UID (display order) | `E004011102030405` |
| Memory | 28 four-byte blocks containing consecutive bytes `00..6F` |
| Block locks | All clear |
| DSFID / AFI / IC reference | `12` / `34` / `01` |
| EAS / EAS and AFI password protection | Inactive / disabled |
| EAS/AFI password | `00000000` |

Edit `memory[28][4]`, `locks[28]` and `image`. Keep the 28-block geometry,
the UID prefix `E00401` and `(uid[3] & 0x18) == 0x10` for SLIX identification.
Start validates/copies the complete image; reader writes change backend RAM.
Reset reloads the original demo.

Responses run in the RX interrupt. Call `nfc_poll()` for activity LED updates
when enabled.
High-rate, single-subcarrier replies are supported.
[Model behavior and limits →](../../docs/emulation.md#nfc-v-behavior)

## Check

With one Proxmark3 client:

```text
hf 15 info
hf 15 raw -ac -d 622305040302110104E0001B
```

The second command reads all 28 blocks with security bytes. Expect response
flags `00`, then one lock byte `00` and four memory bytes per block,
followed by a valid CRC. The memory bytes must match `00..6F`.
[Emulation API →](../../docs/emulation.md)
