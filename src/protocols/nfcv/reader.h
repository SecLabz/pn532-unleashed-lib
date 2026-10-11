#pragma once

#include "phy/nfcv/pcd/nfcv_pcd_rx.h"
#include "phy/reader_frame.h"
#include "pn532_unleashed/nfcv.h"
#include "pn532_unleashed/status.h"

#include <stdbool.h>

/* Foreground only. Publish output only after a complete valid operation.
 * Physical exchange policy and field settling remain private to this module. */
nfc_status_t nfcv_reader_scan(nfcv_tag_t *tag);
nfc_status_t nfcv_reader_slix_read(nfcv_slix_dump_t *dump);
bool nfcv_reader_stop(void);
nfc_status_t nfcv_reader_open(bool continuous_field);
nfc_status_t nfcv_reader_transfer_frame(const reader_frame_t *tx,
                                        nfcv_pcd_rx_result_t *rx,
                                        uint32_t sof_timeout_us,
                                        bool expect_response);
