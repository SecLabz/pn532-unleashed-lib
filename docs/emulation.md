# Emulation API

Initialize first, then supply complete state.
Start validates/copies the image before RF; sources can then be reused.
Reader writes change backend RAM. Start requires idle RF.
NFC-A and NFC-V responses are handled directly in RF interrupts.

## `emulation_start(image)`

A synthetic generic ISO15693 tag. Edit UID, memory, AFI and DSFID directly;
all blocks start unlocked.

```c
#include "pn532_unleashed.h"

nfc_status_t start_iso15693_demo(void)
{
    static const uint8_t memory[64][4] = {
        {0x01, 0x02, 0x03, 0x04},
        {0x10, 0x20, 0x30, 0x40},
    };
    static const uint8_t locks[64] = {0};
    static const uint8_t uid[8] = {0xe0, 0x04, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05};
    static const card_image_t image = {
        .model = CARD_ISO15693,
        .uid = uid,
        .uid_size_bytes = sizeof(uid),
        .memory = &memory[0][0],
        .memory_size_bytes = sizeof(memory),
        .state.nfcv = {
            .locks = locks,
            .block_count = 64,
            .dsfid = 0x12,
            .afi = 0x34,
            .ic_ref = 0x56,
        },
    };
    const nfc_status_t status = emulation_start(&image);
    if (status == NFC_RF_ERROR)
    {
        const nfc_status_t cleanup = nfc_stop();
        return cleanup != NFC_OK ? cleanup : status;
    }
    return status;
}
```

For SLIX use 28 blocks; SLIX-L uses 8. Change model and supply its complete
vendor/lock/password state. UIDs are display order; passwords are four RF-order
bytes, least significant first. Unused passwords must be zero.
[Complete SLIX example →](../examples/slix_emulator/main.c)

Rejected starts preserve existing RF roles. For other models, supply the
geometry and protocol state described in [card state](images.md).
[Complete examples](../examples/README.md) show Classic, Type 2 and SLIX images.

### Type 2 behavior

- Locks are one-way. Dynamic lock groups cover two pages on EV1 128/NTAG213,
  sixteen on NTAG215/216. Undefined bytes retain supplied values; fixed physical
  `BD` dynamic-lock readback is not synthesized. Manufacturer bytes remain
  unchanged.
- EV1 supports INCR_CNT, CHECK_TEARING_EVENT and VCSL. NTAG21x uses its automatic
  READ/FAST_READ counter at index 2 and rejects those EV1 commands.
- CFGLCK protection applies at start. Setting it updates RAM but defers protection
  until reload; selection/HALT/wakeup do not apply it. Password/PACK access is
  separate. Physical field-loss reset is not modeled.

Rules follow [NTAG21x](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)
and [Ultralight EV1](https://www.nxp.com/docs/en/data-sheet/MF0ULX1.pdf) datasheets.

### NFC-V behavior

Generic ISO15693, SLIX and SLIX-L support supplied memory and block locks.
SLIX adds EAS/AFI passwords; SLIX-L also supports privacy/destroy.
Decoding supports 1-of-4/1-of-256; replies use high-rate, single-subcarrier
modulation. Low-rate, two-subcarrier and delayed-EOF exchanges are unsupported.

### ISO-DEP images

ISO-DEP provides supplied identity/activation. ISO4, ST25TA and MIFARE Plus add
limited local replies. DESFire supplies public version, directories, settings
and readable data. Authenticated files, secure messaging, transactions and
MIFARE Plus secure messaging are unsupported.

## `emulation_snapshot()`

Export before stop, with no NFC exchanges for the entire call. Reads can
change counters. RX/interrupts stay enabled; concurrent changes are not detected.

```c
#include "pn532_unleashed.h"

static uint8_t storage[CARD_STORAGE_MAX_BYTES];
static card_image_t snapshot;

nfc_status_t export_image(void)
{
    /* Ensure the external reader is no longer exchanging NFC frames. */
    return emulation_snapshot(
        &snapshot, storage, sizeof(storage));
}
```

The independent copy includes current memory, credentials, counters, locks
and vendor state, excluding transient sessions. Returned pointers refer to caller
storage. After stop, reload it with `emulation_start(&snapshot)`. Stop does not
save RAM; another start reloads supplied state.

Failure preserves the output and storage. Custom handlers and
destroyed SLIX-L are unsupported; export requires active built-in emulation.
[Snapshot contract →](../include/pn532_unleashed/emulation.h)

## `nfca_emulation_start()` / `nfcv_emulation_start()`

Copy the [NTAG Custom Handler example](../examples/ntag_custom_handler/main.c).
`nfca_emulation_start()` / `nfcv_emulation_start()` borrow exclusive,
initialized state until successful stop. Both callbacks and their critical
callees run in RAM inside the RX IRQ.

Never allocate, log, wait, write flash, call lifecycle APIs or use SPI/HSU in
handlers. RX stays disabled through handling/TX. Replies allow 542 NFC-A bytes
including CRC, or 321 NFC-V bytes excluding PHY-added CRC. Invalid output is
discarded. [Callback contracts →](../include/pn532_unleashed/emulation.h)
