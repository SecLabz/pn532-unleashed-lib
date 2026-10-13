#include "cards/mifare/reader/mifare_recovery.h"

#include "cards/mifare/reader/mifare_reader.h"
#include "cards/nfca_access.h"
#include "common/endian.h"
#include "phy/nfca/pcd/nfca_recovery_clock.h"

#include "hardware/sync.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include <string.h>

static bool clock_active;

bool mifare_recovery_prepare(void)
{
    if (nfca_selected_tag() == NULL ||
        mifare_model_config(nfca_selected_model()) == NULL)
    {
        return false;
    }
    if (!clock_active)
    {
        crypto1_prepare();
        nfca_recovery_clock_start();
        clock_active = true;
    }
    /* Attack acquisition starts plaintext AUTH, invalidating cached cipher. */
    mifare_access_reader_reset();
    return true;
}
const nfca_tag_t *mifare_recovery_tag(void)
{
    return nfca_selected_tag();
}
void mifare_recovery_reset(void)
{
    if (clock_active)
    {
        nfca_recovery_clock_stop();
    }
    clock_active = false;
    mifare_darkside_reset();
}
nfc_status_t mifare_recovery_reselect(void)
{
    return nfca_reader_reselect_known(nfca_selected_tag(), 100u);
}

bool mifare_recovery_valid_block(uint8_t block)
{
    const mifare_product_config_t *config =
        mifare_model_config(nfca_selected_model());
    return config != NULL && block < config->block_count;
}

nfc_status_t mifare_recovery_expire(void)
{
    const reader_frame_t dummy = {.data = {0u}, .length = 1u};
    if (!nfca_pcd_send(&dummy))
    {
        return NFC_RF_ERROR;
    }
    /* PM3's 848 SSP clocks at fc/4; preserve cadence without rounding to us. */
    const uint32_t deadline = nfca_recovery_clock_now() + 212u;
    return nfca_recovery_clock_wait(deadline) ? NFC_OK : NFC_RF_ERROR;
}

nfc_status_t nfca_mifare_probe(uint32_t *nonce)
{
    if (!mifare_recovery_prepare())
    {
        return NFC_BUSY;
    }
    if (nonce == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    nfc_status_t status = mifare_recovery_reselect();
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    mifare_reader_command(&tx, MIFARE_CMD_AUTH_A, 0u);
    if (status == NFC_OK)
    {
        status = nfca_reader_transfer(&tx, &rx, MIFARE_NONCE_BYTES * 8u);
    }
    if (status == NFC_OK &&
        (rx.size_bits != MIFARE_NONCE_BYTES * 8u || !rx.parity_valid))
    {
        status = NFC_RF_ERROR;
    }
    if (status == NFC_OK)
    {
        *nonce = read_be32(rx.data);
        status = mifare_recovery_expire();
    }
    return status;
}

nfc_status_t nfca_mifare_collect(const mifare_source_t *source,
                                 uint8_t target_block,
                                 bool target_key_b,
                                 bool scheduled,
                                 mifare_observation_t *observation)
{
    if (!mifare_recovery_prepare())
    {
        return NFC_BUSY;
    }
    if (source == NULL || observation == NULL ||
        !mifare_recovery_valid_block(source->block) ||
        !mifare_recovery_valid_block(target_block))
    {
        return NFC_INVALID_ARGUMENT;
    }
    nfc_status_t status = mifare_recovery_reselect();
    if (status != NFC_OK)
    {
        return status;
    }
    uint32_t nonce = 0u;
    crypto1_state_t cipher = {0};
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    /* Mask only this complete scheduled authentication pair. Preparation must
     * precede the initial deadline; all work stays on the owning core. */
    const uint32_t reader_nonce = get_rand_32();
    const uint32_t saved = scheduled ? save_and_disable_interrupts() : 0u;
    const uint32_t deadline =
        scheduled ? nfca_recovery_clock_now() + MIFARE_TX_PREPARE_TICKS : 0u;
    status = mifare_reader_authenticate(nfca_selected_tag(),
                                        source->block,
                                        source->key_b,
                                        source->key,
                                        &cipher,
                                        &nonce,
                                        deadline,
                                        reader_nonce);
    if (status == NFC_OK)
    {
        mifare_reader_command(&tx,
                              target_key_b ? MIFARE_CMD_AUTH_B
                                           : MIFARE_CMD_AUTH_A,
                              target_block);
        mifare_reader_encrypt(&cipher, &tx, 0u);
        status = nfca_reader_transfer_at(&tx,
                                         &rx,
                                         MIFARE_NONCE_BYTES * 8u,
                                         scheduled ? deadline + 4096u : 0u);
    }
    crypto1_clear(&cipher);
    if (scheduled)
    {
        restore_interrupts(saved);
    }
    if (status == NFC_OK && rx.size_bits != MIFARE_NONCE_BYTES * 8u)
    {
        status = NFC_RF_ERROR;
    }
    if (status != NFC_OK)
    {
        return status;
    }
    mifare_observation_t staged = {
        .source_nonce = nonce,
        .encrypted_nonce = read_be32(rx.data),
    };
    memcpy(staged.parity, rx.parity, sizeof(staged.parity));
    status = mifare_recovery_expire();
    if (status == NFC_OK)
    {
        *observation = staged;
    }
    return status;
}
