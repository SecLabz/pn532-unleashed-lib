# NTAG Custom Handler

A small Type 2 command handler over the shared RF engine, with synthetic data.
[Source to copy →](main.c) · [Build target `ntag_custom_handler` →](../README.md)

| Field | Demo value |
| --- | --- |
| UID / RF-order ATQA / SAK | `04112233445566` / `44 00` / `00` |
| GET_VERSION | `00 04 04 02 01 00 0F 03` |
| Memory | 45 four-byte pages; user pages 4..39 use a byte-offset pattern |

## Customize

Edit `demo_tag.memory` and `demo_tag.version`. UID comes from pages 0/1;
update BCC0 (`88 XOR UID0 XOR UID1 XOR UID2`) and BCC1
(`UID3 XOR UID4 XOR UID5 XOR UID6`) when changing it.
Startup rejects inconsistent BCC. The library borrows the RAM context until stop.

Add commands in `demo_ntag_command()`, keeping it and its callback in RAM.
The activation helper handles REQA/WUPA, selection and HALT. READ wraps four
pages at the end of memory. Handlers must not log, allocate or use SPI/HSU.

This demo supports GET_VERSION, READ and invalid-command NACK. It omits
FAST_READ, passwords, signatures, counters, writes, locks and Amiibo behavior.
Use built-in image emulation for complete supported NTAG behavior.
The capability container advertises read-only access; the pattern is not NDEF.

## Check

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
READ 44 wraps through pages 0..2; READ 45 and writes NACK.
Host checks cover handler dispatch, malformed requests, BCC, wrap and HALT.
Generic tag-info tools may report omitted features.
[Callback API →](../../docs/emulation.md#nfca_emulation_start--nfcv_emulation_start)
