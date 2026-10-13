#pragma once

#include "pn532_unleashed/config.h"
#include "pn532_unleashed/status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Numeric values are stable across source-level enum reordering. */
    typedef enum
    {
        NFC_STATE_UNINITIALIZED = 0,
        NFC_STATE_IDLE = 1,
        NFC_STATE_READING_NFCA = 6,
        NFC_STATE_READING_NFCV = 4,
        NFC_STATE_EMULATING_NFCA = 2,
        NFC_STATE_EMULATING_NFCV = 3,
        NFC_STATE_ERROR = 5,
        NFC_STATE_SNIFFING_NFCA = 7,
        NFC_STATE_SNIFFING_NFCV = 8,
    } nfc_state_t;

    /* Single instance, foreground calls on one core, no concurrent/reentrant
     * calls. The application owns SDK initialization, the required system
     * clock, USB, watchdog and recovery. See docs/library.md for reserved
     * resources. init is idempotent while idle and returns BUSY during RF
     * activity. It never changes the clock. Frontend failure enters ERROR;
     * init/stop may retry. config is copied; NULL uses defaults on first init
     * and preserves the selected transport and pins on retries. Invalid
     * transport, active GPIOs or duplicate pins return INVALID_ARGUMENT before
     * hardware access. Transport and pins are fixed once resources are claimed,
     * even if frontend setup fails; a different selection returns
     * INVALID_ARGUMENT until MCU reset. A clock or resource conflict does not
     * commit the configuration. */
    /* A failed custom reader session must be stopped before reinitialization.
     */
    nfc_status_t nfc_init(const nfc_config_t *config);

    /* Service bounded passive capture and update the activity LED. Call
     * regularly from the main loop
     */
    void nfc_poll(void);

    /* Stop capture and TX, disconnect SIGIN and request field-off/idle.
     * Stops the current role regardless of which caller started it; do not
     * call after a competing operation returned BUSY. Repeated calls are safe.
     * A frontend cleanup failure returns RF_ERROR and enters ERROR; no new RF
     * role may start until cleanup succeeds. */
    nfc_status_t nfc_stop(void);
    nfc_state_t nfc_state(void);

    /* Cached PN532 identification and SAM setup succeeded since reset.
     * No live health probe or bus IO; remains true after nfc_stop(). */
    bool nfc_frontend_initialized(void);
    /* A tag is armed, a reader configured or passive capture configured;
     * false while idle. Does not
     * imply that the reader field is currently on. No bus IO. */
    bool nfc_rf_ready(void);

#ifdef __cplusplus
}
#endif
