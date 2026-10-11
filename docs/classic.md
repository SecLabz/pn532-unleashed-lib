# Classic API

Initialize first, with idle RF. Stop acquired sessions on every exit and check
cleanup. Complete magic/UID operations release their own RF operation.
Supply known credentials explicitly.

## Blocks and sectors

Use an open [`nfca_select()`](nfca.md#nfca_selectexpected-model) session.
Keys are six bytes; `key_b` selects Key B instead of Key A.
Authentication is cached for the current sector/key.

| Function | Output / behavior |
| --- | --- |
| `nfca_mifare_read_block(block, key_b, key, data, writable)` | One sixteen-byte block; outputs change only on success |
| `nfca_mifare_read_sector(sector, key_b, key, memory, capacity, size)` | Four or sixteen blocks, including the trailer; size is zero on failure |
| `nfca_mifare_write_block(block, key_b, key, before, after)` | Check permissions, compare sixteen bytes, write once and verify |

Masked trailer secrets remain unknown. `writable` describes user-data
permission. Block 0 and trailers cannot be written through this API.
`NFC_CONTENT_CHANGED` means no write was sent; `NFC_WRITE_OUTCOME_UNKNOWN`
forbids automatic retry. Ordinary failures close selection; successful calls
keep it. A `NULL` sector-size output is rejected without changing RF ownership.

## `nfca_mifare_read_sector()`

Read sector 1 with its Key A. Adjust model, sector or key type;
small sectors need 64 bytes, large 4K sectors need 256.

```c
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfca_mifare.h"

#include <string.h>

nfc_status_t
read_sector1(const uint8_t key[6], uint8_t memory[64], uint16_t *size_bytes)
{
    nfca_tag_t tag;
    nfc_status_t status = nfca_scan(&tag);
    if (status != NFC_OK) { return status; }
    status = nfca_select(&tag, CARD_CLASSIC_1K);
    nfc_status_t cleanup;
    *size_bytes = 0;
    if (status != NFC_OK)
    {
        return status;
    }
    status = nfca_mifare_read_sector(1, false, key, memory, 64, size_bytes);

    cleanup = nfc_stop();
    return cleanup != NFC_OK ? cleanup : status;
}
```

Use output only on success; masked trailer secrets remain unknown.

## Key checks and nonce observations

| Function in an open `nfca_select()` session | Result |
| --- | --- |
| `nfca_mifare_probe()` | One fresh AUTH nonce |
| `nfca_mifare_check_key()` | Check one supplied key; ACCESS_DENIED means no match |
| `nfca_mifare_collect()` | One source/target nonce observation with raw parity |
| `nfca_mifare_darkside_start()` / `_step()` | Explicit bounded steps and typed outcomes |

Keep the selection for successive calls; stop on completion or error.
Collection uses
`scheduled = true` for fixed-deadline Nested work, `false` for Hardnested.
Darkside `DARKSIDE_MORE` requests another step, not a recovered key.
[Acquisition contracts →](../include/pn532_unleashed/nfca_mifare.h)

## `nfca_mifare_magic()`

Scan a Classic identity and probe signatures. `NULL` enables read-only probes.

```c
#include "pn532_unleashed.h"
#include "pn532_unleashed/nfca_mifare_magic.h"

nfc_status_t detect_magic(mifare_magic_result_t *result)
{
    nfca_tag_t expected;
    nfc_status_t status = nfca_scan(&expected);
    if (status != NFC_OK)
    {
        return status;
    }
    return nfca_mifare_magic(&expected, NULL, false, result);
}
```

Flags report observed signatures; absence is inconclusive. Probes verify the
same identity before publishing. A supplied key also enables AUTH and the first
block-0 WRITE phase, followed by field-off. No write data is sent.
[Probe contract →](../include/pn532_unleashed/nfca_mifare_magic.h)

## `nfca_mifare_set_uid()`

Use a known generation-specific method and expected identity.
This separate API performs a physical write:

```c
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfca_mifare_uid.h"

#include <string.h>

nfc_status_t change_gen1a_uid(const nfca_tag_t *expected,
                              const uint8_t uid[4])
{
    if (expected->uid_size_bytes != 4)
    {
        return NFC_INVALID_ARGUMENT;
    }
    mifare_uid_request_t request = {
        .method = UID_GEN1A,
        .expected = *expected,
    };
    memcpy(request.uid, uid, 4);
    return nfca_mifare_set_uid(&request);
}
```

UID length stays 4 or 7 bytes. The operation preserves block-0 metadata,
recomputes BCC and verifies activation. Gen2/FUID/GDM need a six-byte key;
GTU uses a four-byte password (`NULL` means zero). Its `_uid_probe()` is
read-only. RF stops on return; never retry an uncertain write.
[Methods and credentials →](../include/pn532_unleashed/nfca_mifare_uid.h)
