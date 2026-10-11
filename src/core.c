#include "api_internal.h"
#include "board/activity_led.h"
#include "board/board_config.h"
#include "board/resources.h"
#include "cards/image_internal.h"
#include "cards/mifare/reader/mifare_recovery.h"
#include "cards/nfca_access.h"
#include "emulation/emulation.h"
#include "pn532/pn532_frontend.h"
#include "pn532/pn532_transport.h"
#include "pn532_unleashed.h"
#include "protocols/nfca/reader.h"
#include "protocols/nfcv/reader.h"

#include "hardware/clocks.h"

static struct
{
    bool initialized;
    bool resources_claimed;
    bool reader_session;
    nfc_state_t state;
} context;

/* Built-in operations own RF until their explicit cleanup completes. Unlike
 * public raw-reader sessions, they cannot be resumed through session APIs. */
nfc_status_t nfc_operation_begin(nfc_state_t state)
{
    const nfc_status_t status = nfc_operation_require(NFC_STATE_IDLE);
    if (status != NFC_OK)
    {
        return status;
    }
    context.state = state;
    return NFC_OK;
}

void nfc_operation_end(nfc_status_t status)
{
    context.state = status == NFC_RF_ERROR ? NFC_STATE_ERROR : NFC_STATE_IDLE;
}

nfc_status_t nfc_session_begin(nfc_state_t state)
{
    const nfc_status_t status = nfc_operation_require(NFC_STATE_IDLE);
    if (status != NFC_OK)
    {
        return status;
    }
    context.state = state;
    context.reader_session = true;
    return NFC_OK;
}

nfc_status_t nfc_operation_require(nfc_state_t state)
{
    if (!context.initialized)
    {
        return NFC_NOT_INITIALIZED;
    }
    if (context.state == NFC_STATE_ERROR)
    {
        return NFC_RF_ERROR;
    }
    if (context.state != state)
    {
        return NFC_BUSY;
    }
    return clock_get_hz(clk_sys) == PNU_REQUIRED_SYS_CLOCK_HZ ? NFC_OK
                                                              : NFC_WRONG_CLOCK;
}

nfc_status_t nfc_session_require(nfc_state_t state)
{
    const nfc_status_t status = nfc_operation_require(state);
    if (status != NFC_OK)
    {
        return status;
    }
    return context.reader_session ? NFC_OK : NFC_BUSY;
}

void nfc_session_error(void)
{
    context.state = NFC_STATE_ERROR;
}

nfc_status_t nfc_init(const nfc_config_t *config)
{
    if (context.reader_session && context.state == NFC_STATE_ERROR)
    {
        /* A failed custom transaction can still own a live field. Recovery
         * must explicitly stop/release that session before reinitialization. */
        return NFC_RF_ERROR;
    }
    if (context.state != NFC_STATE_UNINITIALIZED &&
        context.state != NFC_STATE_IDLE && context.state != NFC_STATE_ERROR)
    {
        return NFC_BUSY;
    }
    const nfc_config_t *selected = config != NULL ? config : &board_config.pins;
    if (!board_config_validate(selected) ||
        (context.resources_claimed && !board_config_matches(selected)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (clock_get_hz(clk_sys) != PNU_REQUIRED_SYS_CLOCK_HZ)
    {
        return NFC_WRONG_CLOCK;
    }
    if (context.initialized && context.state == NFC_STATE_IDLE)
    {
        return NFC_OK;
    }
    if (!context.resources_claimed &&
        !board_resources_claim(selected->transport))
    {
        return NFC_RESOURCE_BUSY;
    }
    if (!context.resources_claimed)
    {
        context.resources_claimed = true;
        board_config_apply(selected);
        pn532_transport_select(selected->transport);
        board_activity_led_init();
    }
    if (!pn532_frontend_init() || !pn532_frontend_stop())
    {
        context.state = NFC_STATE_ERROR;
        return NFC_RF_ERROR;
    }
    context.initialized = true;
    context.state = NFC_STATE_IDLE;
    return NFC_OK;
}

void nfc_poll(void)
{
    if (context.state == NFC_STATE_ERROR)
    {
        board_activity_led_clear();
        return;
    }
    board_activity_led_task();
}

nfc_status_t nfc_stop(void)
{
    if (context.state == NFC_STATE_UNINITIALIZED)
    {
        return NFC_OK;
    }
    context.reader_session = false;
    nfca_access_reset();
    mifare_recovery_reset();
    const bool nfca_stopped = nfca_reader_stop();
    const bool reader_stopped = nfcv_reader_stop();
    const bool tag_stopped =
        context.initialized ? emulation_stop() : pn532_frontend_stop();
    board_activity_led_clear();
    if (!nfca_stopped || !reader_stopped || !tag_stopped)
    {
        context.state = NFC_STATE_ERROR;
        return NFC_RF_ERROR;
    }
    context.state =
        context.initialized ? NFC_STATE_IDLE : NFC_STATE_UNINITIALIZED;
    return NFC_OK;
}

nfc_state_t nfc_state(void)
{
    return context.state;
}

nfc_status_t emulation_start(const card_image_t *image)
{
    if (image_validate(image) != IMAGE_OK)
    {
        return NFC_INVALID_IMAGE;
    }
    const nfc_status_t status = nfc_operation_require(NFC_STATE_IDLE);
    if (status != NFC_OK)
    {
        return status;
    }
    if (!emulation_load(image))
    {
        context.state = NFC_STATE_ERROR;
        return NFC_RF_ERROR;
    }
    context.state = card_model_is_nfca(image->model) ? NFC_STATE_EMULATING_NFCA
                                                     : NFC_STATE_EMULATING_NFCV;
    return NFC_OK;
}

nfc_status_t nfca_emulation_start(nfca_handler_t handler, void *user)
{
    if (handler == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_operation_require(NFC_STATE_IDLE);
    if (status != NFC_OK)
    {
        return status;
    }
    if (!emulation_start_nfca(handler, user))
    {
        context.state = NFC_STATE_ERROR;
        return NFC_RF_ERROR;
    }
    context.state = NFC_STATE_EMULATING_NFCA;
    return NFC_OK;
}

nfc_status_t nfcv_emulation_start(nfcv_handler_t handler, void *user)
{
    if (handler == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_operation_require(NFC_STATE_IDLE);
    if (status != NFC_OK)
    {
        return status;
    }
    if (!emulation_start_nfcv(handler, user))
    {
        context.state = NFC_STATE_ERROR;
        return NFC_RF_ERROR;
    }
    context.state = NFC_STATE_EMULATING_NFCV;
    return NFC_OK;
}
