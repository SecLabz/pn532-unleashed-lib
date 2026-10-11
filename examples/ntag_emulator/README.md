# NTAG Emulator

Start a synthetic NTAG213 using the library's built-in card behavior.
[Source to copy →](main.c) · [Build target `ntag_emulator` →](../README.md)

| Demo field | Value |
| --- | --- |
| UID | `04112233445566` |
| Memory | 45 four-byte pages; user pages 4..39 use a byte-offset pattern |
| Locks / password protection | Clear locks; AUTH0 `FF` (unprotected) |
| Password / PACK | `FFFFFFFF` / `0000` |
| Version | `00 04 04 02 01 00 0F 03` |
| Signature / counters / attempts | Zero, synthetic demonstration state |
| Tearing flags | `BD BD BD` |

Edit `memory[45][4]` and `state`. UID comes from pages 0/1; update BCC0
(`88 XOR UID0 XOR UID1 XOR UID2`) and BCC1
(`UID3 XOR UID4 XOR UID5 XOR UID6`) when changing it.
The raw data pattern is not NDEF, and the signature is not authentic NXP data.

Built-in behavior handles activation, reads, supported writes, passwords,
locks and counters. Start validates/copies the complete image before RF;
reader writes change backend RAM. Reset reloads the original demo.
[Model behavior and limits →](../../docs/emulation.md#type-2-behavior)

## Check

With one Proxmark3 client:

```text
hw sethfthresh -t 15
hf 14a reader --skip
hf 14a raw -3sc 60
hf 14a raw -3sc 3004
hw fpgaoff
hw sethfthresh -t 7
```

Expect the version above and
`10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F`.
The SPI bench passed 20/20 version and read checks at PM3 threshold 15.
Compatibility with other readers needs separate on-air validation.
[Emulation API →](../../docs/emulation.md)
