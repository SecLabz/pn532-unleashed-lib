#pragma once

#include "pn532_unleashed/nfcv_memory_store.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NFCV_STANDARD_READY = 0,
        NFCV_STANDARD_SELECTED,
        NFCV_STANDARD_QUIET,
    } nfcv_standard_state_t;

    typedef struct
    {
        uint8_t flags;
        uint8_t command;
        const uint8_t *params;
        uint8_t params_size_bytes;
        bool inventory;
        bool addressed;
        bool selected;
        bool option;
    } nfcv_request_t;

    typedef enum
    {
        NFCV_STANDARD_UNHANDLED = 0,
        NFCV_STANDARD_NO_RESPONSE,
        NFCV_STANDARD_RESPONSE,
    } nfcv_standard_result_t;

    typedef struct
    {
        nfcv_standard_state_t state;
        uint8_t uid[NFCV_UID_SIZE];
        uint8_t dsfid;
        uint8_t afi;
        uint8_t ic_ref;
        bool dsfid_locked;
        bool afi_locked;
        nfcv_memory_store_t *memory;
    } nfcv_standard_t;

    /* Borrow a caller-owned initialized store for the context lifetime; copy
     * the eight-byte display UID and metadata. Require matching SLIX/SLIX-L
     * UID/geometry or 64 generic blocks with an E0-prefixed UID. Invalid input
     * leaves standard unchanged. This context is independent of the library's
     * active RF card. */
    bool nfcv_standard_init(nfcv_standard_t *standard,
                            nfcv_memory_store_t *memory,
                            const uint8_t uid[NFCV_UID_SIZE],
                            uint8_t dsfid,
                            uint8_t afi,
                            uint8_t ic_ref,
                            bool dsfid_locked,
                            bool afi_locked);
    /* Reset only session state; metadata and memory remain. All setters
     * require non-NULL initialized contexts. set_uid copies eight bytes;
     * caller must preserve the supported model/geometry relationship. */
    void nfcv_standard_reset_session(nfcv_standard_t *standard);
    void nfcv_standard_quiet(nfcv_standard_t *standard);
    void nfcv_standard_set_uid(nfcv_standard_t *standard,
                               const uint8_t uid[NFCV_UID_SIZE]);

    /* Validate a decoded frame and addressing. False means do not use the
     * request output. On success request.params borrows frame storage until
     * handle returns; keep it alive and unchanged. No RF work is performed. */
    bool nfcv_standard_prepare(const nfcv_standard_t *standard,
                               const nfcv_frame_t *frame,
                               nfcv_request_t *request);
    /* Require a successful prepare for this context, non-NULL outputs and
     * live request storage. Produces response payload or silence; vendor
     * commands may return UNHANDLED. Mutable state belongs to the caller. */
    nfcv_standard_result_t nfcv_standard_handle(nfcv_standard_t *standard,
                                                const nfcv_request_t *request,
                                                nfcv_tag_response_t *response);

#ifdef __cplusplus
}
#endif
