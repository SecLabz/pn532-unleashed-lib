#pragma once

#include "pn532_unleashed/nfca_reader.h"
#include "pn532_unleashed/status.h"

#define DESFIRE_MAX_ATS_BYTES 32u
#define DESFIRE_MAX_REPLY_BYTES 1024u

/* Caller-owned ISO-DEP session at 106 kbit/s, CID/NAD disabled. Start requires
 * an existing public NFC-A reader role; stop it with nfc_stop on every exit.
 * ATS includes TL, without CRC. Exchanges allow only public DESFire commands,
 * assemble ISO-DEP chains and native AF continuations locally, and never retry
 * a command. Each exchange is bounded to 750 ms, 64 physical blocks and 8 WTX.
 * FWT above 100 ms is unsupported. Outputs are valid only on NFC_OK; a native
 * error status is a successful physical exchange. No pointers are retained. */
typedef struct
{
    nfca_tag_t tag;
    uint8_t ats[DESFIRE_MAX_ATS_BYTES];
    uint8_t ats_size_bytes;
    uint8_t tx_block;
    uint8_t rx_block;
    uint16_t frame_size_bytes;
    uint32_t fwt_us;
    bool active;
} desfire_session_t;

#ifdef __cplusplus
extern "C"
{
#endif

    nfc_status_t desfire_activate(desfire_session_t *session);
    /* command includes native opcode and parameters, not PCB/CRC. reply
     * receives concatenated data without native status, at most 1024 bytes.
     * Capacity and size are bytes. Invalid arguments do not transmit. Transport
     * errors invalidate the session; BUFFER_TOO_SMALL after RX also invalidates
     * it. */
    nfc_status_t desfire_exchange(desfire_session_t *session,
                                  const uint8_t *command,
                                  size_t command_size_bytes,
                                  uint8_t *reply,
                                  size_t capacity_bytes,
                                  size_t *size_bytes,
                                  uint8_t *native_status);

#ifdef __cplusplus
}
#endif
