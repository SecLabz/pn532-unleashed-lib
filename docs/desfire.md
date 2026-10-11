# Public DESFire exchanges

Bounded native commands over ISO-DEP at 106 kbit/s, using a public NFC-A reader
session. Authentication/secure messaging are unsupported.

## `desfire_activate()` / `desfire_exchange()`

Initialize with idle RF. Return the 28 public version bytes and stop on every
exit after acquiring the reader; use output only after success.

```c
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfca_desfire.h"

nfc_status_t read_desfire_version(uint8_t version[28])
{
    desfire_session_t session;
    nfc_status_t status = nfca_reader_start();
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
    status = desfire_activate(&session);
    if (status != NFC_OK)
    {
        goto done;
    }
    const uint8_t command[] = {0x60};
    size_t size_bytes = 0;
    uint8_t native_status = 0xff;
    status = desfire_exchange(&session,
                                  command,
                                  sizeof(command),
                                  version,
                                  28,
                                  &size_bytes,
                                  &native_status);
    if (status != NFC_OK)
    {
        goto done;
    }
    if (native_status != 0 || size_bytes != 28)
    {
        status = NFC_INVALID_RESPONSE;
    }

done:
    cleanup = nfc_stop();
    return cleanup != NFC_OK ? cleanup : status;
}
```

Keep the caller-owned session for successive commands. Inspect `native_status`:
a card error can be a successful physical exchange.

Stop the reader after a failed activation or exchange. Commands are never
retried after silence. Public reads depend on card permissions.
[Session contract and limits](../include/pn532_unleashed/nfca_desfire.h).
