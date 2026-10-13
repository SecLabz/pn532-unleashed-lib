# NFC-A and NFC-V sniffing

Capture NFC-A and NFC-V reader/tag exchanges.

## Add the AUX connections

Keep the existing PN532 wiring and common ground. Add these inputs for sniffing:

| Protocol | PN532 → RP2040 defaults |
| --- | --- |
| NFC-A | AUX1 → GPIO0 and AUX2 → GPIO1 |
| NFC-V | AUX1 → GPIO0; AUX2 is unused |

Set `aux1_gpio` and, for NFC-A, `aux2_gpio` in the sniff configuration to use
other free pins. Avoid pins already used by SIGIN, SIGOUT, SPI/HSU or the board.

## Run a complete example

Choose [NFC-A](../examples/nfca_sniffer/main.c) or
[NFC-V](../examples/nfcv_sniffer/main.c). Both programs initialize the PN532,
start sniffing, capture for ten seconds, stop and print frames with a summary.

From the library root:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
./examples/build.sh nfca_sniffer SPI
./examples/build.sh nfcv_sniffer SPI
```

Use `HSU` instead of `SPI` for that connection. Follow the
[example setup](../examples/README.md#load-and-run) to load the UF2.
Open USB stdio with DTR, place the reader and tag, then press Enter.
The example stores the first 16 frames and prints them after capture stops.
Change its pin assignments, duration or storage capacity in `main.c`.

## Use it in your application

Follow the example's capture loop: call `nfc_poll()` continuously, then read
available frames. Keep the average polling interval below 1 ms and avoid pauses
longer than 30 ms. Do not put sleeps or blocking output in that loop.

A read returns a copied frame and `available=true`, or `available=false` when
there is nothing to consume. Call `nfc_stop()` when finished, then read any last
frames and the summary. They remain available until the next capture starts.
Only one reader, emulation or sniff operation can run at a time.

If capture enters ERROR, stop before starting another operation. If stop fails,
resolve the PN532 connection and retry cleanup. Slow consumers can lose frames;
check the summary's drop and overrun counters.

## Limits

NFC-A capture supports 106 kbit/s. NFC-V supports reader 1-of-4/1-of-256 and
high-rate single-subcarrier replies. SPI capture is tested on the reference
board; HSU capture and other rates/encodings are not qualified on air.
Reception depends on antenna placement and can miss frames.

NFC-A retains up to 32 reader bytes and 64 tag bytes; NFC-V retains 64 and 323.
CRC bytes are included. Partial NFC-A reader bytes and oversized frames are
marked TRUNCATED. DECODED means complete framing; inspect CRC/parity flags
separately. Timestamps are sampled carrier cycles, not calibrated RF timing.
The public headers document the individual fields and status values:
[NFC-A](../include/pn532_unleashed/nfca_sniff.h),
[NFC-V](../include/pn532_unleashed/nfcv_sniff.h).
