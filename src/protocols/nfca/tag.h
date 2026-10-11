#pragma once

#include "pn532_unleashed/nfca_frame.h"

/* Private streaming response; the decoded frame stays at offset zero. The
 * extra type value is never returned by the public activation API. */
#define NFCA_TAG_RESPONSE_DEFERRED_PARITY ((nfca_tag_response_type_t)4)

typedef struct nfca_tag_response nfca_tag_response_t;
struct nfca_tag_response
{
    nfca_response_t frame;
    /* DEFERRED_PARITY only: the low bit of byte zero is ready. After
     * queueing SOF and that bit, the backend requests byte zero, then the
     * remaining bytes while the first byte is on air. len must be nonzero;
     * the backend calls prepare_bytes exactly once per range, in order.
     * The producer preserves the precomputed first bit and must fit its
     * FIFO airtime budget; no allocation or PHY calls. prepare_context must
     * remain valid until transmission completes. */
    void (*prepare_bytes)(nfca_tag_response_t *response,
                          uint16_t first,
                          uint16_t count);
    void *prepare_context;
};
