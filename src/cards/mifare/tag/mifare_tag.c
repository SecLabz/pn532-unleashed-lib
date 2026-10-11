#include "cards/mifare/mifare_access.h"
#include "cards/mifare/mifare_commands.h"
#include "cards/mifare/mifare_product.h"
#include "cards/mifare/tag/mifare_tag_session.h"
#include "common/compiler.h"
#include "common/crypto1.h"
#include "common/endian.h"
#include "emulation/snapshot.h"
#include "phy/nfca/picc/nfca_frame.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "phy/timing.h"
#include "pn532_unleashed/nfca_activation.h"
#include "protocols/nfca/nfca_profile.h"

static nfca_activation_t activation;
static mifare_product_t product;
static mifare_access_t access;
static mifare_tag_session_t session;
static mifare_commands_t commands;
static bool reset_pending;

void nfca_mifare_get_image(card_image_t *image)
{
    *image = (card_image_t){
        .model = product.config->model,
        .uid = product.uid,
        .uid_size_bytes = sizeof(product.uid),
        .memory = &product.memory[0][0],
        .memory_size_bytes = product.config->block_count * MIFARE_BLOCK_SIZE,
    };
}

static void RAM_FUNC(reset_product_session)(void)
{
    reset_pending = false;
    mifare_tag_session_reset(&session, &access);
    mifare_commands_reset(&commands);
}

static void mifare_classic_init(const mifare_product_config_t *config)
{
    reset_pending = false;
    crypto1_prepare();
    nfca_tx_set_fdt_base(NFCA_TAG_FDT_BASE_CYCLES);
    mifare_product_init(&product, config);
    nfca_activation_init(&activation,
                         product.uid,
                         sizeof(product.uid),
                         product.config->atqa,
                         product.config->sak);
    mifare_access_init(&access, product.memory, product.config->block_count);
    mifare_tag_session_init(&session, &access);
    mifare_commands_init(
        &commands, product.memory, product.uid, &access, &session);
}

bool nfca_mifare_load_image(const card_image_t *image)
{
    if (image_validate(image) != IMAGE_OK)
    {
        return false;
    }
    const mifare_product_config_t *config = mifare_model_config(image->model);
    if (config == NULL)
    {
        return false;
    }
    mifare_classic_init(config);
    if (!mifare_product_load(&product, image))
    {
        return false;
    }
    (void)nfca_activation_set_uid(
        &activation, product.uid, sizeof(product.uid));
    reset_product_session();
    return true;
}

static bool
RAM_FUNC(mifare_classic_handle_frame)(const nfca_tag_request_t *frame,
                                      nfca_tag_response_t *response)
{
    if (session.state == MIFARE_STATE_AUTHENTICATED && !frame->short_frame)
    {
        const mifare_command_result_t result =
            mifare_commands_handle(&commands, frame, response);
        if (result == MIFARE_COMMAND_HALT)
        {
            nfca_activation_halt(&activation, frame->timestamp_us);
            return false;
        }
        return result == MIFARE_COMMAND_RESPONSE;
    }

    if (session.state == MIFARE_STATE_AUTHING &&
        frame->size_bytes == MIFARE_READER_AUTH_BYTES)
    {
        return mifare_tag_session_finish(&session, &access, frame, response);
    }

    /* A selected card's valid AUTH cannot be an activation command. Avoid
     * constructing anticollision replies before dispatching it. */
    if (nfca_activation_is_active(&activation) &&
        (match_cmd(frame, MIFARE_CMD_AUTH_A, MIFARE_COMMAND_FRAME_BYTES) ||
         match_cmd(frame, MIFARE_CMD_AUTH_B, MIFARE_COMMAND_FRAME_BYTES)))
    {
        return mifare_tag_session_begin(&session,
                                        &access,
                                        product.memory,
                                        product.uid,
                                        frame->bytes[0],
                                        frame->bytes[1],
                                        false,
                                        response);
    }

    nfca_activation_event_t event;
    const nfca_activation_result_t activation_result =
        nfca_activation_handle(&activation, frame, &response->frame, &event);
    if (activation_result != NFCA_ACTIVATION_UNHANDLED)
    {
        if (event != NFCA_ACTIVATION_EVENT_NONE)
        {
            /* Clear session data after ATQA/SAK TX, before RX is rearmed. */
            reset_pending = true;
        }
        return activation_result == NFCA_ACTIVATION_RESPONSE;
    }

    return false;
}

static void RAM_FUNC(mifare_classic_prepare_next_exchange)(void)
{
    if (reset_pending)
    {
        reset_product_session();
    }
    mifare_tag_session_prepare_answers(&session);
    mifare_access_update_cache(&access);
    mifare_commands_prepare_next_request(&commands);
}

const nfca_tag_profile_t nfca_mifare_profile = {
    .handle_frame = mifare_classic_handle_frame,
    .prepare_next_exchange = mifare_classic_prepare_next_exchange,
};
