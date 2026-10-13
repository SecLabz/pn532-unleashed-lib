#pragma once

#include "phy/nfca/pcd/nfca_pcd.h"
#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/status.h"

/* One synchronous session keeps the field on until stop. No host work may
 * run during an exchange. Stop is required after success or failure. */
nfc_status_t nfca_reader_open_selected(nfca_tag_t *tag);
nfc_status_t nfca_reader_open(void);
nfc_status_t nfca_reader_select_tag(nfca_tag_t *tag);
nfc_status_t nfca_reader_transfer_frame(const reader_frame_t *tx,
                                        nfca_pcd_response_t *rx,
                                        uint32_t sof_timeout_us,
                                        bool expect_response);
/* Single wakeup attempt for positioning; the complete selection is bounded. */
nfc_status_t nfca_reader_scan_start(nfca_tag_t *tag);
bool nfca_reader_stop(void);
nfc_status_t nfca_reader_transfer(const reader_frame_t *tx,
                                  nfca_pcd_response_t *rx,
                                  uint16_t max_bits);

/* Stop the field, wait 5 ms, reopen and verify the expected identity.
 * Caller still owns mandatory stop on every exit, including failure. */
nfc_status_t nfca_reader_restart(const nfca_tag_t *expected);

/* WUPA and selection must preserve the observed identity before more reads. */
nfc_status_t nfca_reader_reselect(const nfca_tag_t *expected);
/* Address the previously observed full UID; validate ATQA and SAK replies.
 * Guard is after capture/decode, before the encoded 352/fc carrier guard. */
nfc_status_t nfca_reader_reselect_known(const nfca_tag_t *expected,
                                        uint32_t guard_us);

/* Scheduled exchange uses fc/16 ticks and bypasses the ordinary 100 us guard.
 * The protocol owner must establish its own frame-delay deadline. */
nfc_status_t nfca_reader_transfer_at(const reader_frame_t *tx,
                                     nfca_pcd_response_t *rx,
                                     uint16_t max_bits,
                                     uint32_t deadline_ticks);

/* Protocol-specific silence deadline; capture and physical EOF stay unchanged.
 */
nfc_status_t nfca_reader_transfer_timed(const reader_frame_t *tx,
                                        nfca_pcd_response_t *rx,
                                        uint16_t max_bits,
                                        uint32_t sof_timeout_us,
                                        uint32_t guard_us);
