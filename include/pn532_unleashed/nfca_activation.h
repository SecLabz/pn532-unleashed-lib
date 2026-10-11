#pragma once

#include "pn532_unleashed/nfca_frame.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NFCA_ACTIVATION_MAX_UID_BYTES NFCA_UID_TRIPLE_BYTES

    typedef enum
    {
        NFCA_ACTIVATION_UNHANDLED = 0,
        NFCA_ACTIVATION_SILENT,
        NFCA_ACTIVATION_RESPONSE,
    } nfca_activation_result_t;

    typedef enum
    {
        NFCA_ACTIVATION_EVENT_NONE = 0,
        NFCA_ACTIVATION_EVENT_READY,
        NFCA_ACTIVATION_EVENT_ACTIVE,
        NFCA_ACTIVATION_EVENT_HALTED,
    } nfca_activation_event_t;

    typedef enum
    {
        NFCA_ACTIVATION_IDLE = 0,
        NFCA_ACTIVATION_READY,
        NFCA_ACTIVATION_ACTIVE,
        NFCA_ACTIVATION_HALTED,
    } nfca_activation_state_t;

    typedef struct
    {
        nfca_activation_state_t state;
        uint8_t uid[NFCA_ACTIVATION_MAX_UID_BYTES];
        uint8_t uid_size_bytes;
        uint8_t atqa[NFCA_ATQA_BYTES];
        uint8_t sak;
        uint8_t selected_cascade;
        uint32_t halt_started_us;
    } nfca_activation_t;

    /* Pure protocol state, independent of the active RF instance. Init copies
     * UID (4, 7 or 10 bytes) and two-byte ATQA and resets all session state.
     * Invalid pointers or UID size return false without changing activation. */
    bool nfca_activation_init(nfca_activation_t *activation,
                              const uint8_t *uid,
                              uint8_t uid_size_bytes,
                              const uint8_t atqa[NFCA_ATQA_BYTES],
                              uint8_t sak);

    /* Copy a four-, seven- or ten-byte UID and reset activation. Invalid
     * arguments return false with activation unchanged. */
    bool nfca_activation_set_uid(nfca_activation_t *activation,
                                 const uint8_t *uid,
                                 uint8_t uid_size_bytes);

    /* Resets activation state while preserving the configured identity. */
    void nfca_activation_reset(nfca_activation_t *activation);

    /* Enters HALT at timestamp_us without decoding a plaintext HLTA frame. */
    void nfca_activation_halt(nfca_activation_t *activation,
                              uint32_t timestamp_us);

    /* Non-NULL initialized context, request, response and event are required.
     * Handles REQA/WUPA, cascade, SELECT and HLTA. RESPONSE publishes type
     * and payload; SILENT sets NONE; UNHANDLED leaves response unchanged.
     * event is always written. Request storage is borrowed for this call.
     * No PHY, PN532 configuration or active card state is accessed. */
    nfca_activation_result_t
    nfca_activation_handle(nfca_activation_t *activation,
                           const nfca_tag_request_t *request,
                           nfca_response_t *response,
                           nfca_activation_event_t *event);

    static inline bool
    nfca_activation_is_active(const nfca_activation_t *activation)
    {
        return activation->state == NFCA_ACTIVATION_ACTIVE;
    }

#ifdef __cplusplus
}
#endif
