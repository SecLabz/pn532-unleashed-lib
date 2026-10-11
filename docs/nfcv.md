# NFC-V API

Initialize first, with idle RF. UIDs use display order, starting `E0`.

## `nfcv_scan(tag)`

Read identity and system information for ISO15693 tags with 1..255 blocks
of 1..32 bytes. The output changes only on success.

## `nfcv_slix_read(dump)`

Read complete SLIX/SLIX-L memory and one security byte per block.
SLIX has 28 four-byte blocks; SLIX-L has eight. Reader writes are unsupported.
Use static storage for the dump; use output only after `NFC_OK`.
A failed read reports zero `memory_size_bytes`.

```c
static nfcv_slix_dump_t dump;
nfc_status_t status = nfcv_slix_read(&dump);
```

Both functions release acquired RF before return. Rejection preserves existing
roles. Scans attempt once; memory exchanges allow three attempts. The field
settles for 20 ms and toggles between exchanges.

[Full contracts](../include/pn532_unleashed/nfcv.h) ·
[Emulation behavior](emulation.md#nfc-v-behavior)

## Raw reader exchanges

Custom sessions keep RF on until stop. This reads one four-byte ISO15693
block using a high-rate, non-addressed command. Keep only one tag in the field.
Loop over valid blocks to build a generic readable-memory dump.

```c
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfcv_reader.h"

#include <string.h>

nfc_status_t read_nfcv_block(uint8_t block, uint8_t data[4])
{
    nfc_status_t status = nfcv_reader_start();
    nfc_status_t cleanup;
    if (status != NFC_OK)
    {
        if (status == NFC_RF_ERROR)
        {
            cleanup = nfc_stop();
            return cleanup != NFC_OK ? cleanup : status;
        }
        return status;
    }
    nfcv_identity_t tag;
    status = nfcv_reader_inventory(&tag);
    if (status != NFC_OK)
    {
        goto done;
    }
    uint8_t bytes[22];
    nfcv_reader_reply_t reply = {
        .data = bytes,
        .capacity_bytes = sizeof(bytes),
    };
    const uint8_t command[] = {0x02, 0x20, block};
    status = nfcv_reader_exchange(command, sizeof(command), &reply, NULL);
    if (status != NFC_OK)
    {
        goto done;
    }
    if (reply.size_bytes != 5 || (bytes[0] & 0x01) != 0)
    {
        status = NFC_INVALID_RESPONSE;
    }

done:
    cleanup = nfc_stop();
    if (cleanup != NFC_OK)
    {
        return cleanup;
    }
    if (status != NFC_OK)
    {
        return status;
    }
    memcpy(data, &bytes[1], 4);
    return NFC_OK;
}
```

An unsuccessful start leaves competing RF roles intact. Only `NFC_RF_ERROR`
requires explicit recovery cleanup before another start.

Reader TX uses 1-of-4; replies use high-rate, single-subcarrier modulation.
Card errors are valid RF replies: inspect response flags.

| Exchange | TX / RX limit | Default/max SOF wait |
| --- | --- | --- |
| Simple `nfcv_reader_exchange()` | 30 / 22 bytes without CRC, including flags | 8000 us |
| Exact `nfcv_reader_exchange_frame()` | 32 / 24 bytes including CRC/flags | 8000 us |

Exact success proves a physical frame; validate CRC and semantics before
continuing. `NULL` options use defaults; SOF waits can only be reduced.
Discovery silence keeps the session; required-response failure needs stop.
`READER_RESPONSE_NONE` proves TX only. A small buffer can fail after TX;
never retry an uncertain mutation.

[Raw NFC-V contracts](../include/pn532_unleashed/nfcv_reader.h) ·
[Shared exchange contracts](../include/pn532_unleashed/reader.h)
