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
| `nfca_mifare_collect_static()` | One static-nonce observation, optionally advancing the known source |
| `nfca_mifare_darkside_start()` / `_step()` | Explicit bounded steps and typed outcomes |

Keep the selection for successive calls; stop on completion or error. Key
checks use fresh authentication, reuse a verified plaintext selection, and
address the known full UID when reselection is necessary. A successful check
sends encrypted HALT before WUPA/SELECT; a failed candidate is also reselected
before returning. Ordinary reads can safely follow either result. Classic
AUTH waits at most 1 ms for SOF in each part
([NXP MF1S50YYX/V1 table 21](https://cache.nxp.com/docs/en/data-sheet/MF1S50YYX_V1.pdf));
capture still requires the same physical EOF. Ordinary AUTH and key-check
reselection use a 66 us guard plus the unchanged 352/fc pre-SOF carrier; this exceeds 1172/fc without relying
on CPU work or post-EOF capture. Scheduled acquisition and GDM authentication keep their
existing deadlines.
Collection uses
`scheduled = true` for fixed-deadline Nested work, `false` for Hardnested.
Unscheduled Hardnested uses the Classic 66 us guard during known-identity
reselection and target authentication, with a 1 ms target SOF timeout. Scheduled
Nested and Static Nested retain their existing cadence. The physical capture
grid, response validation, cipher cleanup and challenge expiration are unchanged.
The reference application records on-air qualification separately.
`nfca_mifare_collect_static(source, target_block, target_key_b, advance_source,
observation)` adds a bounded Static Nested acquisition path. It completes one
known authentication, or two when `advance_source` is true, then captures and
expires the unknown target challenge. The observation records the last known
source nonce. Encrypted nonce parity and both known authentication responses
are validated physically; outputs change only on success. No card memory is
written. Existing collection signatures and behavior remain unchanged.
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

## Explicit trailer management

In an expected-identity `nfca_select()` session,
`nfca_mifare_trailer_check(sector, key_b, key, before, after)` checks a proposed
replacement without writing. `nfca_mifare_trailer_write()` performs the same
fresh checks and sends WRITE once. Both functions release the owned selection.
The borrowed 16-byte before array is the exact observable trailer; after
contains both new keys, valid access bytes and GPB. Preserve metadata explicitly
when only changing keys. Manufacturer/user blocks cannot be targeted here.

Credentials, access permissions and compare-before are verified immediately
before writing. A denied key field may only be preserved, proven by
authentication or a readable-Key-B comparison. The reader then reauthenticates
and compares again. New A, usable B or readable B, and metadata are verified
afterward. Unknown write outcomes forbid automatic retry. Generic user-data
writes continue to reject all trailers.
