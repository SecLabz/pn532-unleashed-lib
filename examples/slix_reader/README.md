# SLIX reader loop

Read all observable SLIX/SLIX-L memory/security and print UID, DSFID, AFI,
IC reference and geometry. [Source to copy →](main.c)

Build target `slix_reader` with the [shared instructions](../README.md).
Open USB stdio with DTR. Example output for the synthetic SLIX tag:

```text
SLIX tag UID: E004010102030405
DSFID: 12  AFI: 34  IC reference: 56
Memory: 28 blocks x 4 bytes
Block 00 [open]: 00 01 02 03
Block 01 [locked]: 04 05 06 07
```

Only complete successful reads are printed. RF stops before output and the
one-second pause. `NFC_NO_TAG` waits; other errors report status. Failed cleanup
stops the loop while keeping USB recovery available.

The result excludes hidden passwords/state and is not a complete emulation
image. No reader writes or password guessing.
[Raw reader exchanges →](../../docs/nfcv.md#raw-reader-exchanges)
