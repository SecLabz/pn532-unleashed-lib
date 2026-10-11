#pragma once

#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfca_frame.h"
#include "pn532_unleashed/nfcv_tag.h"
#include "pn532_unleashed/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Validate the entire image before RF setup, then copy into the card
     * backend. The source may be reused after return. Invalid input leaves
     * state untouched. Start requires initialized IDLE; active RF returns BUSY.
     * On RF failure RX and TX stop and state becomes ERROR. Writes affect
     * backend RAM only. stop does not export or save RAM; another start reloads
     * the supplied image. */
    nfc_status_t emulation_start(const card_image_t *image);

    /* Copy active built-in card state into output and caller byte storage.
     * CARD_STORAGE_MAX_BYTES is sufficient. The returned state is independent
     * of backend RAM, with pointers into storage. No concurrent RF exchanges
     * for the complete call; RX/interrupts stay enabled. No allocation or RF
     * reconfiguration. Output/storage unchanged on failure. Custom handlers
     * and destroyed SLIX-L are unsupported. No source/output/storage overlap.
     * Call outside callbacks from the owning foreground. */
    nfc_status_t emulation_snapshot(card_image_t *output,
                                    uint8_t *storage,
                                    size_t capacity_bytes);

    /* Requests and responses are borrowed only during the call. The engine
     * initializes response control fields to silence, checks its type/length,
     * transmits, then rearms RX. Never allocate, log, wait, call the lifecycle
     * API or use SPI/HSU here. Both run in the RX IRQ; place the handler and
     * its critical callees in RAM and fit the existing RF deadline.
     * NFC-A data includes any required CRC; NFC-V excludes CRC. */
    typedef void (*nfca_handler_t)(void *context,
                                   const nfca_tag_request_t *request,
                                   nfca_response_t *response);
    typedef void (*nfcv_handler_t)(void *context,
                                   const nfcv_frame_t *request,
                                   nfcv_tag_response_t *response);

    /* Require initialized IDLE and a non-NULL handler. The context may be NULL
     * for a stateless handler. Initialize it before start; it is borrowed and
     * must remain alive, exclusively owned by the handler, until stop returns.
     * Start does not copy/reset card state. Invalid arguments preserve RF.
     * RF failure stops capture/TX and enters ERROR. Successful stop guarantees
     * no subsequent callback. Malformed handler output is silently discarded.
     * These use the same physical engine as emulation_start(image). */
    nfc_status_t nfca_emulation_start(nfca_handler_t handler, void *context);
    nfc_status_t nfcv_emulation_start(nfcv_handler_t handler, void *context);

#ifdef __cplusplus
}
#endif
