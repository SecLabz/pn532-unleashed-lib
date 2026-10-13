#pragma once

#include "pn532_unleashed/status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Optional bench API; foreground on the same RF owner core as the core
     * API. Available only when its matching CMake trace option is enabled. */
#ifdef PNU_RF_TRACE
    uint8_t nfc_trace_gsn(void);
    uint16_t nfc_trace_fdt_fc(void);

    /* Idle only. gsn accepts 0x08, 0x80 or 0xf0; base_fc accepts 900 or
     * 996 carrier cycles. Validate both settings before mutation. Returns
     * INVALID_ARGUMENT for unsupported values or BUSY during active RF;
     * failure leaves both settings unchanged. Applied before the next tag
     * emulation; experimental settings are not production RF defaults. */
    nfc_status_t nfc_trace_configure(uint8_t gsn, uint16_t base_fc);

    /* Caller owns non-NULL output with capacity_bytes available. Read only
     * after RF stops. No bus IO: bounded cached records only. Text sizes
     * exclude NUL; zero means unavailable or no complete output fits.
     * NFC-A offset counts records, not bytes; output starts with the ATR1
     * header followed by complete records. Reading never clears records. */
    uint16_t nfc_trace_frontend(char *output, uint16_t capacity_bytes);
    uint16_t
    nfc_trace_nfca(uint16_t offset, uint8_t *output, uint16_t capacity_bytes);
    uint16_t nfc_trace_reader(char *output, uint16_t capacity_bytes);
    /* Passive sniff bench snapshot, read after STOP only. Offset counts
     * 16-byte records. Each reply has an 80-byte SNR1 header: completed word
     * count, segment count, three edge/high counters, four segment start_fc
     * values and sizes. Records contain three packed sample words and decoder
     * state bits (synchronized, PCD active, PICC active, previous pause).
     * Four bounded 19.3 ms bursts follow activity after 1 s of quiet input.
     * The trace may fill; it never blocks capture or performs bus IO. */
    uint16_t
    nfc_trace_sniff(uint16_t offset, uint8_t *output, uint16_t capacity_bytes);
    /* Attach the completed foreground reader operation status to its record. */
    void nfc_trace_finish(uint8_t status);
#endif

#ifdef PNU_HSU_TRACE
    /* Same output ownership/text size contract; cached HSU diagnostics only.
     * No live bus IO. Returns zero for an unselected or SPI transport. */
    uint16_t nfc_trace_transport(char *output, uint16_t capacity_bytes);
#endif

#ifdef __cplusplus
}
#endif
