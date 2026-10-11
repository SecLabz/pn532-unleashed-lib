#include "emulation/emulation.h"

#include "board/activity_led.h"
#include "board/tag_board.h"
#include "cards/image_internal.h"
#include "phy/nfca/picc/nfca_rx.h"
#include "phy/nfca/picc/nfca_trace.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "phy/nfcv/picc/nfcv_rx.h"
#include "phy/nfcv/picc/nfcv_tx.h"
#include "phy/picc/picc_phy.h"
#include "pn532/pn532_frontend.h"
#ifdef PNU_RF_TRACE
#include "pn532/pn532_registers.h"
#endif
#include "protocols/nfca/nfca_profile.h"
#include "protocols/nfcv/nfcv_profile.h"

#include "pico/stdlib.h"

static struct
{
    const nfca_tag_profile_t *nfca;
    const nfcv_tag_profile_t *nfcv;
    nfca_handler_t nfca_handler;
    nfcv_handler_t nfcv_handler;
    void *user;
    card_model_t model;
    bool is_nfca;
    bool capture_started;
} engine;

#ifdef PNU_RF_TRACE
static uint16_t trace_fdt_base_fc = EMULATION_TRACE_DEFAULT_FDT_BASE_FC;

nfc_status_t nfc_trace_configure(uint8_t gsn, uint16_t base_fc)
{
    if ((gsn != CIU_GS_N_OFF_MOD_8 && gsn != CIU_GS_N_OFF_CW_8 &&
         gsn != CIU_GS_N_OFF_CW_MAX) ||
        (base_fc != EMULATION_TRACE_EARLY_FDT_BASE_FC &&
         base_fc != EMULATION_TRACE_DEFAULT_FDT_BASE_FC))
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (!pn532_frontend_trace_set_gsn(gsn))
    {
        return NFC_BUSY;
    }
    trace_fdt_base_fc = base_fc;
    return NFC_OK;
}

uint16_t nfc_trace_fdt_fc(void)
{
    return trace_fdt_base_fc;
}
#endif

static void __not_in_flash_func(handle_nfca)(const nfca_frame_t *frame)
{
    if (engine.nfca == NULL && engine.nfca_handler == NULL)
    {
        return;
    }
    const nfca_tag_request_t request = {
        .bytes = frame->bytes,
        .parity = frame->parity,
        .size_bytes = frame->size_bytes,
        .short_frame = frame->short_frame,
        .parity_ok = frame->parity_ok,
        .crc_ok = frame->crc_ok,
        .timestamp_us = time_us_32(),
    };
    nfca_tag_response_t response;
    bool reply;
    if (engine.nfca_handler != NULL)
    {
        nfca_response_silent(&response.frame);
        engine.nfca_handler(engine.user, &request, &response.frame);
        const nfca_response_t *output = &response.frame;
        reply = (output->type == NFCA_TAG_RESPONSE_SHORT4 &&
                 output->size_bytes == 0u &&
                 output->short4 <= NFCA_SHORT_RESPONSE_MASK) ||
                ((output->type == NFCA_TAG_RESPONSE_DATA ||
                  output->type == NFCA_TAG_RESPONSE_DATA_WITH_PARITY) &&
                 output->size_bytes != 0u &&
                 output->size_bytes <= sizeof(output->bytes));
    }
    else
    {
        reply = engine.nfca->handle_frame(&request, &response);
    }
    if (reply)
    {
        nfca_tx_send_response(&response);
        NFCA_TRACE_RESPONSE(&response);
    }
    if (engine.nfca != NULL && engine.nfca->prepare_next_exchange != NULL)
    {
        engine.nfca->prepare_next_exchange();
    }
    /* Record only after TX and next-exchange preparation, outside FDT. */
    board_activity_led_record(reply, true);
}

static void __not_in_flash_func(handle_nfcv)(const nfcv_frame_t *frame,
                                             uint32_t eof_time_us)
{
    if (engine.nfcv == NULL && engine.nfcv_handler == NULL)
    {
        return;
    }
    nfcv_tag_response_t response;
    bool reply;
    if (engine.nfcv_handler != NULL)
    {
        nfcv_reply_silent(&response);
        engine.nfcv_handler(engine.user, frame, &response);
        reply = response.size_bytes != 0u &&
                response.size_bytes <= NFCV_TAG_MAX_RESPONSE_BYTES;
    }
    else
    {
        reply = engine.nfcv->handle_frame(frame, &response) &&
                response.size_bytes != 0u;
    }
    if (reply)
    {
        reply = nfcv_tx_send_response(
            eof_time_us, response.bytes, response.size_bytes);
    }
    board_activity_led_record(reply, true);
}

bool emulation_stop(void)
{
    if (engine.capture_started)
    {
        if (engine.is_nfca)
        {
            nfca_rx_deinit();
        }
        else
        {
            nfcv_rx_deinit();
        }
    }
    engine.nfca = NULL;
    engine.nfcv = NULL;
    engine.nfca_handler = NULL;
    engine.nfcv_handler = NULL;
    engine.user = NULL;
    engine.model = 0;
    engine.is_nfca = false;
    engine.capture_started = false;
    tag_phy_stop();
    return pn532_frontend_stop();
}

static bool prepare(bool is_nfca)
{
    if (!emulation_stop())
    {
        return false;
    }
    NFCA_TRACE_RESET();
    engine.is_nfca = is_nfca;
    if (!pn532_frontend_prepare_tag(is_nfca ? PN532_PROTOCOL_NFCA
                                            : PN532_PROTOCOL_NFCV))
    {
        return false;
    }
    tag_board_init();
    return true;
}

static bool arm(void)
{
    if (engine.is_nfca)
    {
        if (!tag_phy_init())
        {
            (void)emulation_stop();
            return false;
        }
#ifdef PNU_RF_TRACE
        nfca_tx_set_fdt_base(trace_fdt_base_fc * NFC_SYS_CLOCKS_PER_FC);
#else
        nfca_tx_set_fdt_base(NFCA_TAG_FDT_BASE_CYCLES);
#endif
        engine.capture_started = nfca_rx_init(handle_nfca);
    }
    else
    {
        if (!nfcv_tx_init())
        {
            (void)emulation_stop();
            return false;
        }
        engine.capture_started = nfcv_rx_init(handle_nfcv);
    }
    if (!engine.capture_started || !pn532_frontend_arm_tag())
    {
        (void)emulation_stop();
        return false;
    }
    return true;
}

bool emulation_load(const card_image_t *image)
{
    if (image == NULL || !prepare(card_model_is_nfca(image->model)))
    {
        return false;
    }
    bool loaded;
    if (engine.is_nfca && card_model_is_classic(image->model))
    {
        engine.nfca = &nfca_mifare_profile;
        loaded = nfca_mifare_load_image(image);
    }
    else if (engine.is_nfca && card_model_is_iso4(image->model))
    {
        engine.nfca = &nfca_iso4_profile;
        loaded = nfca_iso4_load_image(image);
    }
    else if (engine.is_nfca)
    {
        engine.nfca = &nfca_ntag_profile;
        loaded = nfca_ntag_load_image(image);
    }
    else
    {
        engine.nfcv = &nfcv_vicinity_profile;
        loaded = nfcv_load_image(image);
    }
    if (!loaded)
    {
        (void)emulation_stop();
        return false;
    }
    engine.model = image->model;
    return arm();
}

card_model_t emulation_model(void)
{
    return engine.model;
}

bool emulation_start_nfca(nfca_handler_t handler, void *context)
{
    if (handler == NULL || !prepare(true))
    {
        return false;
    }
    engine.nfca_handler = handler;
    engine.user = context;
    return arm();
}

bool emulation_start_nfcv(nfcv_handler_t handler, void *context)
{
    if (handler == NULL || !prepare(false))
    {
        return false;
    }
    engine.nfcv_handler = handler;
    engine.user = context;
    return arm();
}
