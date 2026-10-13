# NFC-V sniffer

Capture for ten seconds, then print the first 16 reader/tag frames and totals.
[Complete source →](main.c)

Build `nfcv_sniffer` using the [shared instructions](../README.md).
Add AUX1 → GPIO0 to the existing wiring.
Open USB stdio with DTR and press Enter when the reader and tag are ready.
Output is printed after capture stops, so printing does not delay polling.

Change the AUX pins, capture duration or local frame capacity in `main.c`.
[Library usage and limits →](../../docs/sniff.md)
