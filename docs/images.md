# Card state API

`card_image_t` is the same typed state passed to `image_validate()` and
`emulation_start()`, and returned by `emulation_snapshot()`.

## `card_image_t`

Common fields are model, UID, full memory and byte sizes.

- Pointers borrow caller storage during validation/start.
- Start copies all state before RF begins; input buffers may then be reused.
- Use static storage for large state descriptors and memory buffers.

| Model family | Common memory | Protocol state |
| --- | --- | --- |
| Classic Mini/1K/2K/4K | 320/1024/2048/4096 bytes, including keys/access bits | Zero |
| Plain Ultralight | 64 bytes | Zero |
| EV1 48/128, NTAG213/215/216 | 80/164/180/540/924 bytes | `state.type2` |
| Generic ISO15693, SLIX, SLIX-L | 256/112/32 bytes | `state.nfcv` |
| ISO-DEP, ISO4, ST25TA, Plus | None | `state.iso4`: RF-order ATQA, final SAK, complete ATS including TL |
| DESFire | None | `state.iso4.capture` |

Classic has a four-byte UID; Type 2 seven; NFC-V eight in display order.
ISO-DEP family accepts four, seven or ten. Type 2 memory includes manufacturer,
locks/configuration/password pages. Anticollision identity is stored separately;
loading does not rewrite memory to match UID. Supplied NTAG215 Amiibo images
are supported. Masked reader observations may lack required hidden state.

## Protocol state

| State | Fields |
| --- | --- |
| Type 2 | Version, signature, three 24-bit counters, tearing, authentication attempts |
| NFC-V | One 0/1 lock per block, DSFID/AFI/IC reference, vendor flags, passwords |
| ISO4 | RF-order ATQA, final SAK, complete ATS including TL |
| DESFire | Version/free-memory observations, applications, files, settings/data blobs |

## `image_validate()`

Checks model, UID, memory geometry and required protocol state. Returns
`IMAGE_OK` or a specific `image_status_t`; input and RF state stay unchanged.
`image_status_string()` returns a static diagnostic.

DESFire arrays are bounded to 28 applications and 64 total files. Blob sizes,
ranges, duplicate IDs/AIDs and captured public access are validated. Native
unavailable states stay explicit; no secrets or authenticated data are inferred.
`reports_unavailable` selects explicit unavailable responses for missing
observations, instead of not-found responses.

[Snapshot ownership and usage](emulation.md#emulation_snapshot).

[Public state fields](../include/pn532_unleashed/image.h) ·
[DESFire capture fields](../include/pn532_unleashed/desfire_capture.h) ·
[Emulation API](emulation.md)
