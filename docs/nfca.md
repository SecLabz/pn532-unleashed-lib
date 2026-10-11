# NFC-A API

Initialize before use. NFC-A operates at 106 kbit/s and selects one tag;
collisions are rejected. Page addresses refer to four bytes.
[Classic blocks and sectors](classic.md) have a separate API.

## `nfca_scan(tag)`

Discover one UID/ATQA/SAK from idle RF. The field stops before return.
The output changes only on success.

## `nfca_select(expected, model)`

Select the expected UID/ATQA/SAK with an explicit memory model.
Requires idle RF; copies identity and model.

- Success keeps RF on until `nfc_stop()`.
- Rejected selection preserves existing RF roles.
- Failed selection releases any RF it acquired.

## `nfca_read_all(password, dump)`

Read all observable Type 2 pages and available metadata in the selected session.

| Parameter / output | Meaning |
| --- | --- |
| `password` | Four bytes or `NULL`; plain Ultralight accepts `NULL` only |
| `dump->memory_size_bytes` | Observed length; zero on failure |
| `dump->metadata_valid` | Valid version, signature and PACK fields |
| `dump->counter_valid`, `dump->tearing_valid` | Valid entries, bits 0–2 |

Use static storage for the dump. Masked secrets remain unknown.
Authentication may consume password attempts; reads may increment counters.
Discard the dump after failure.

```c
#include "pn532_unleashed.h"

nfc_status_t read_type2(const nfca_tag_t *expected,
                       card_model_t model,
                       const uint8_t *password,
                       nfca_dump_t *dump)
{
    nfc_status_t status = nfca_select(expected, model);
    if (status != NFC_OK)
    {
        return status;
    }
    status = nfca_read_all(password, dump);
    const nfc_status_t cleanup = nfc_stop();
    return status != NFC_OK ? status : cleanup;
}
```

## `nfca_read_page(page, password, data, writable)`

Read one page, including observable manufacturer/configuration pages.
The password is four bytes or `NULL` for unauthenticated access.

- Outputs change only on success.
- `writable` describes user-data permission; false if permissions are unreadable.

## `nfca_write_page(page, password, before, after)`

Check permissions, compare the four `before` bytes, write once, verify `after`.
Manufacturer/UID, OTP, lock and configuration pages are rejected.

| Result | Meaning |
| --- | --- |
| `NFC_OK` | Replacement verified |
| `NFC_CONTENT_CHANGED` | Current bytes differ from `before`; no write sent |
| `NFC_WRITE_OUTCOME_UNKNOWN` | Replacement could not be verified; never retry automatically |

```c
#include "pn532_unleashed.h"

nfc_status_t write_page4(const nfca_tag_t *expected,
                         const uint8_t before[4], const uint8_t after[4])
{
    nfc_status_t status = nfca_select(expected, CARD_NTAG213);
    if (status != NFC_OK)
    {
        return status;
    }
    status = nfca_write_page(4, NULL, before, after);
    const nfc_status_t cleanup = nfc_stop();
    return status != NFC_OK ? status : cleanup;
}
```

## Selected-session lifetime

Successful reads/writes keep selection. Ordinary failures close it.
A `NULL` dump is rejected without changing RF ownership.
Credentials may change between calls; reselection verifies the same identity.
Locks/configuration are cached until stop or reselection.
Raw reader APIs return `NFC_BUSY` while selected access owns RF.

[Full contracts](../include/pn532_unleashed/nfca.h)

## Raw reader exchanges

`nfca_reader_start()` opens a raw session; `nfca_reader_select(tag)` selects
one tag. `nfca_reader_exchange(command, size, reply, options)` sends bytes and
checks CRC/parity. Inspect `is_short4`/`short4` for card responses.
Stop acquired sessions on every exit.

| Exchange | TX / RX limit | Default/max SOF wait |
| --- | --- | --- |
| Simple | 30 / 32 bytes without CRC, or short4 reply | 5000 us |
| Exact `nfca_reader_exchange_frame()` | 256 bits / 34 bytes including CRC | 5000 us |

Exact success proves a physical frame; validate integrity and semantics before
continuing. Explicit parity uses one 0/1 byte per whole byte; encrypted frames
need caller integrity checks.

An unsuccessful start leaves competing RF roles intact. Only `NFC_RF_ERROR`
requires explicit recovery cleanup before another start.

## Receiver settings

Change the NFC-A reader profile while RF is stopped:

```c
bool accepted = nfca_reader_set_rx_profile(NFCA_RX_I_33_DB);
```

| Constant | Channel / gain |
| --- | --- |
| `NFCA_RX_Q_33_DB` | Q / 33 dB, default |
| `NFCA_RX_I_18_DB` | I / 18 dB |
| `NFCA_RX_I_33_DB` | I / 33 dB |
| `NFCA_RX_Q_18_DB` | Q / 18 dB |

Invalid/active changes return false without mutation. Read the choice with
`nfca_reader_rx_profile()`. It applies on next NFC-A reader setup, survives
stop, not reset. NFC-V/emulation are unaffected.

`NULL` options use defaults; SOF waits can only be reduced. Discovery silence
keeps selection; required-response failure needs stop. `READER_RESPONSE_NONE`
proves TX only. A small buffer can fail after TX; never retry an uncertain write.

[Raw NFC-A contracts](../include/pn532_unleashed/nfca_reader.h) ·
[Shared exchange contracts](../include/pn532_unleashed/reader.h)
