#pragma once

#include "phy/timing.h"
#include "pn532_unleashed/diagnostics.h"
#include "pn532_unleashed/emulation.h"
#include "pn532_unleashed/image.h"

#include <stdbool.h>

/* Start copies card contents into its backend after stopping RF. On failure,
 * RX and modulation remain stopped. The uploaded image may then be reused. */
bool emulation_load(const card_image_t *image);
bool emulation_start_nfca(nfca_handler_t handler, void *context);
bool emulation_start_nfcv(nfcv_handler_t handler, void *context);
bool emulation_stop(void);
/* Zero when no built-in image is active, including custom handlers. */
card_model_t emulation_model(void);

#ifdef PNU_RF_TRACE
#define EMULATION_TRACE_DEFAULT_FDT_BASE_FC NFCA_TAG_FDT_BASE_FC
#define EMULATION_TRACE_EARLY_FDT_BASE_FC 900u
#endif
