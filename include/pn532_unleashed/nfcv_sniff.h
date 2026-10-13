#pragma once
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfcv_frame.h"

#define NFCV_SNIFF_MAX_BYTES NFCV_RESPONSE_MAX_BYTES
#define NFCV_SNIFF_DECODED 1u
#define NFCV_SNIFF_CRC_CHECKED 8u
#define NFCV_SNIFF_CRC_VALID 16u
#define NFCV_SNIFF_TRUNCATED 32u

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        NFCV_SNIFF_PCD = 0,
        NFCV_SNIFF_PICC = 1,
    } nfcv_sniff_direction_t;

    /* Dual capture only. Optional wiring is claimed only during sniff.
     * AUX1 carries test bus 2 bit 3; SIGOUT carries the reader envelope.
     * AUX2 is not used. Configuration is copied; application owns the input
     * until nfc_stop. NULL selects AUX1=GPIO0. */
    typedef struct
    {
        uint8_t aux1_gpio;
    } nfcv_sniff_config_t;

    /* Caller-owned data including CRC. PCD capacity 64 bytes, PICC 323 bytes.
     * Sampled GPIO times in carrier cycles, not calibrated RF SOF/FDT.
     * DECODED denotes complete physical framing, independent of CRC validity.
     * TRUNCATED records a bounded prefix, with DECODED clear. */
    typedef struct
    {
        uint64_t start_fc;
        uint32_t duration_fc;
        uint16_t size_bits;
        uint16_t size_bytes;
        nfcv_sniff_direction_t direction;
        uint8_t flags;
        uint8_t data[NFCV_SNIFF_MAX_BYTES];
    } nfcv_sniff_frame_t;

    typedef struct
    {
        uint32_t pcd_frames;
        uint32_t picc_frames;
        uint32_t dropped_frames;
        uint32_t capture_overruns;
        uint32_t invalid_frames;
    } nfcv_sniff_summary_t;

    nfcv_sniff_config_t nfcv_sniff_config_default(void);
    /* Initialized IDLE required. Passive reader 1-of-4/1-of-256 plus tag
     * high-rate single-subcarrier responses. Other tag encodings are not
     * decoded. No RF TX or PN532 transactions during capture. Service with
     * nfc_poll continuously on the owning core. Each call consumes up to 64
     * words (~1.2 ms): average intervals must stay below 1 ms, with transient
     * service gaps no longer than 30 ms. A new start clears the
     * bounded 16-frame FIFO and summary. RF failure/overrun requires nfc_stop;
     * failed register restoration retains ownership until stop succeeds.
     * Experimental test-bus routing is qualified on the reference PN532;
     * other silicon revisions and 10%-ASK downlinks require qualification. */
    nfc_status_t nfcv_sniff_start(const nfcv_sniff_config_t *config);
    /* Nonblocking, during capture or after stop. Empty returns NFC_OK,
     * available=false and leaves frame unchanged. Invalid arguments leave
     * outputs unchanged. Records survive stop until the next start. */
    nfc_status_t nfcv_sniff_read(nfcv_sniff_frame_t *frame, bool *available);
    nfc_status_t nfcv_sniff_summary(nfcv_sniff_summary_t *summary);
#ifdef __cplusplus
}
#endif
