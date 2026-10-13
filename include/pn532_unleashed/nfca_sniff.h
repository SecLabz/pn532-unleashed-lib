#pragma once

#include "pn532_unleashed/core.h"

#include <stddef.h>
#include <stdint.h>

#define NFCA_SNIFF_MAX_BYTES 64u
#define NFCA_SNIFF_DECODED 1u
#define NFCA_SNIFF_PARITY_CHECKED 2u
#define NFCA_SNIFF_PARITY_VALID 4u
#define NFCA_SNIFF_CRC_CHECKED 8u
#define NFCA_SNIFF_CRC_VALID 16u
#define NFCA_SNIFF_TRUNCATED 32u

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        NFCA_SNIFF_PCD = 0,
        NFCA_SNIFF_PICC = 1,
    } nfca_sniff_direction_t;

    /* Optional wiring, copied at start. AUX1=sdata, AUX2=RxActive; both are
     * needed for dual capture. GPIOs are validated against active wiring only
     * when starting sniff. Application owns these pins until stop. */
    typedef struct
    {
        uint8_t aux1_gpio;
        uint8_t aux2_gpio;
    } nfca_sniff_config_t;

    /* Retained data bits excluding parity; CRC bytes are retained. Partial
     * reader bytes are omitted and marked TRUNCATED; tag tails retain exact
     * bit counts. start_fc and duration_fc are sampled GPIO times, not
     * calibrated RF SOF/FDT. No
     * ciphertext CRC/parity interpretation is implied. A truncated frame may
     * contain a prefix; DECODED is clear when physical framing is invalid.
     * PCD frames retain up to 32 bytes; PICC frames up to 64 bytes. Longer
     * frames are explicitly truncated. Fixed caller-owned storage; no pointers
     * retained. */
    typedef struct
    {
        uint64_t start_fc;
        uint32_t duration_fc;
        uint16_t size_bits;
        uint16_t size_bytes;
        nfca_sniff_direction_t direction;
        uint8_t flags;
        uint8_t data[NFCA_SNIFF_MAX_BYTES];
    } nfca_sniff_frame_t;

    typedef struct
    {
        uint32_t pcd_frames;
        uint32_t picc_frames;
        uint32_t dropped_frames;
        uint32_t capture_overruns;
        uint32_t invalid_frames;
    } nfca_sniff_summary_t;

    nfca_sniff_config_t nfca_sniff_config_default(void);
    /* Initialized IDLE required. Passive NFC-A 106 kbit/s only. No RF TX.
     * Call nfc_poll continuously on the owning core. It consumes up to 64
     * words (~1.2 ms) per call: average intervals must stay below 1 ms;
     * transient service gaps must not exceed 30 ms.
     * DMA captures independently of USB. Overrun stops capture, enters ERROR
     * and requires nfc_stop. Hardware failure also requires nfc_stop.
     * NULL config uses AUX1=GPIO0, AUX2=GPIO1. */
    nfc_status_t nfca_sniff_start(const nfca_sniff_config_t *config);
    /* Nonblocking FIFO read during sniff and after stop. Empty returns OK,
     * available=false and leaves frame unchanged. Invalid arguments leave
     * outputs unchanged. A new start clears the queue and summary. */
    nfc_status_t nfca_sniff_read(nfca_sniff_frame_t *frame, bool *available);
    nfc_status_t nfca_sniff_summary(nfca_sniff_summary_t *summary);
#ifdef __cplusplus
}
#endif
