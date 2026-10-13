#include "pn532_unleashed/core.h"
/* Deterministic tag peer: production activation, injected physical faults.
 * This does not simulate PN532 registers, analog modulation or PIO timing. */
#include "cards/image_internal.h"
#include "cards/mifare/mifare_product.h"
#include "cards/mifare/reader/mifare_reader.h"
#include "cards/type2/reader/ntag_reader.h"
#include "emulation/snapshot.h"
#include "phy/nfca/pcd/nfca_pcd_encode.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_activation.h"
#include "pn532_unleashed/nfca_mifare.h"
#include "protocols/nfca/nfca_profile.h"
#include "protocols/nfca/reader.h"

#include <assert.h>
#include <string.h>

#ifdef UNLEASHED_MAGIC_HOST_TEST
bool host_magic_before(const reader_frame_t *tx, nfca_pcd_response_t *rx);
void host_magic_after(const reader_frame_t *tx, nfca_pcd_response_t *rx);
void host_magic_field_off(void);
#endif

bool host_mifare_interrupts_masked(void);

static nfca_activation_t peer;
static const nfca_tag_profile_t *card_peer;
static uint8_t fault;
static bool field;
static uint8_t exchanges;
static unsigned page_writes;
static unsigned classic_writes;
static unsigned selections;
static unsigned authentications;
static unsigned password_authentications;
static unsigned field_stops;

unsigned host_nfca_classic_writes(void)
{
    return classic_writes;
}
unsigned host_nfca_selections(void)
{
    return selections;
}
unsigned host_nfca_authentications(void)
{
    return authentications;
}
unsigned host_nfca_password_authentications(void)
{
    return password_authentications;
}
unsigned host_nfca_field_stops(void)
{
    return field_stops;
}
bool host_nfca_field(void)
{
    return field;
}

void host_nfca_fault(uint8_t error)
{
    fault = error;
}

unsigned host_nfca_page_writes(void)
{
    return page_writes;
}

void host_nfca_peer(const uint8_t *uid, uint8_t size, uint8_t error)
{
    card_peer = NULL;
    const uint8_t atqa[] = {0x44u, 0u};
    (void)nfca_reader_stop();
    (void)nfca_activation_init(&peer, uid, size, atqa, 0u);
    fault = error;
    exchanges = 0u;
}

bool pn532_frontend_prepare_reader(uint8_t protocol)
{
    return protocol == 1u;
}

bool pn532_frontend_set_field(bool enabled)
{
    field = enabled;
    return true;
}

bool pn532_frontend_stop(void)
{
    field_stops += field ? 1u : 0u;
    field = false;
#ifdef UNLEASHED_MAGIC_HOST_TEST
    host_magic_field_off();
#endif
    return fault != 24u || classic_writes == 0u;
}

bool nfca_pcd_init(void)
{
    return true;
}

void nfca_pcd_release(void)
{
}

void sleep_us(uint64_t duration)
{
    assert(!host_mifare_interrupts_masked());
    (void)duration;
}

bool nfca_pcd_exchange(const reader_frame_t *tx,
                       nfca_pcd_response_t *rx,
                       uint16_t max_bits)
{
    (void)max_bits;
    *rx = (nfca_pcd_response_t){0};
    if (!field)
    {
        return false;
    }
    if (fault == 27u && tx->length == 1u && tx->last_byte_bits == 0u)
    {
        return false; /* Target challenge could not be expired. */
    }
#ifdef UNLEASHED_MAGIC_HOST_TEST
    if (host_magic_before(tx, rx))
    {
        return true;
    }
#endif
    exchanges++;
    selections += tx->last_byte_bits == 7u && tx->data[0] == 0x52u ? 1u : 0u;
    if (card_peer != NULL)
    {
        if (card_peer == &nfca_mifare_profile && classic_writes != 0u &&
            tx->length == 4u && tx->data[0] == MIFARE_CMD_AUTH_A &&
            (fault == 22u || fault == 23u))
        {
            /* Change observed metadata/B after an acknowledged write. The
             * peer produces a valid encrypted READ, not a damaged RF frame. */
            card_image_t image;
            nfca_mifare_get_image(&image);
            const uint8_t block =
                mifare_sector_trailer(mifare_block_sector(tx->data[1]));
            uint8_t *trailer = (uint8_t *)image.memory + block * 16u;
            trailer[fault == 22u ? 9u : 10u] ^= 1u;
            fault = 0u;
        }
        const nfca_tag_request_t request = {
            .bytes = tx->data,
            .size_bytes = tx->length,
            .short_frame = tx->last_byte_bits == 7u,
            .parity_ok = true,
            .crc_ok = nfca_check_crc(tx->data, tx->length),
        };
        nfca_tag_response_t response = {0};
        const bool handled = card_peer->handle_frame(&request, &response);
        if (card_peer == &nfca_mifare_profile && tx->length == 4u &&
            response.frame.size_bytes == 4u)
        {
            authentications++;
        }
        if (card_peer == &nfca_ntag_profile && tx->data[0] == NTAG_CMD_PWD_AUTH)
        {
            password_authentications++;
        }
        if (handled && response.prepare_bytes != NULL)
        {
            for (uint16_t byte = 0u; byte < response.frame.size_bytes; byte++)
            {
                response.prepare_bytes(&response, byte, 1u);
            }
        }
        if (handled && response.frame.type == NFCA_TAG_RESPONSE_SHORT4)
        {
            rx->data[0] = response.frame.short4;
            rx->size_bits = 4u;
        }
        else if (handled && response.frame.type != NFCA_TAG_RESPONSE_NONE)
        {
            rx->size_bits = response.frame.size_bytes * 8u;
            memcpy(rx->data, response.frame.bytes, response.frame.size_bytes);
            for (uint16_t byte = 0u; byte < response.frame.size_bytes; byte++)
            {
                rx->parity[byte] = response.frame.type == NFCA_TAG_RESPONSE_DATA
                                       ? nfca_odd_parity_bit(rx->data[byte])
                                       : response.frame.parity[byte];
            }
        }
        if (card_peer->prepare_next_exchange != NULL)
        {
            card_peer->prepare_next_exchange();
        }
        if (card_peer == &nfca_mifare_profile && tx->length == 18u &&
            response.frame.type == NFCA_TAG_RESPONSE_SHORT4)
        {
            classic_writes++;
            if (fault == 13u)
            {
                *rx = (nfca_pcd_response_t){0};
                return true;
            }
        }
        if (tx->length == 8u && tx->data[0] == NTAG_CMD_WRITE &&
            response.frame.type == NFCA_TAG_RESPONSE_SHORT4 &&
            response.frame.short4 == 0x0au)
        {
            page_writes++;
            if (fault == 10u)
            {
                *rx = (nfca_pcd_response_t){0};
                return true;
            }
        }
        rx->framing_valid = rx->size_bits != 0u;
        rx->parity_valid = true;
        if (card_peer == &nfca_mifare_profile && classic_writes != 0u &&
            tx->length == 4u &&
            ((fault == 20u && tx->data[0] == MIFARE_CMD_AUTH_A) ||
             (fault == 21u && tx->data[0] == MIFARE_CMD_AUTH_B)))
        {
            *rx = (nfca_pcd_response_t){0};
        }
        if (card_peer == &nfca_mifare_profile && authentications == 2u &&
            rx->size_bits == 32u)
        {
            if (fault == 25u && tx->length == 4u)
            {
                rx->parity[0] ^= 1u;
            }
            if (fault == 26u && tx->length == 8u)
            {
                rx->data[0] ^= 1u;
            }
        }
        if (fault == 7u && rx->size_bits == 144u)
        {
            rx->parity[17] ^= 1u;
        }
        if (fault == 8u && rx->size_bits == 144u)
        {
            rx->data[16] ^= 1u;
        }
        if (fault == 9u && exchanges > 8u)
        {
            *rx = (nfca_pcd_response_t){0};
        }
        if (fault == 11u && page_writes != 0u && tx->data[0] == NTAG_CMD_READ)
        {
            *rx = (nfca_pcd_response_t){0};
        }
#ifdef UNLEASHED_MAGIC_HOST_TEST
        host_magic_after(tx, rx);
#endif
        return true;
    }
    const nfca_tag_request_t request = {
        .bytes = tx->data,
        .size_bytes = tx->length,
        .short_frame = tx->last_byte_bits == 7u,
        .parity_ok = true,
        .crc_ok = nfca_check_crc(tx->data, tx->length),
    };
    nfca_response_t response = {0};
    nfca_activation_event_t event;
    const nfca_activation_result_t result =
        nfca_activation_handle(&peer, &request, &response, &event);
    if (result != NFCA_ACTIVATION_RESPONSE || fault == 1u)
    {
        return true;
    }
    rx->size_bits = response.size_bytes * 8u;
    memcpy(rx->data, response.bytes, response.size_bytes);
    rx->framing_valid = fault != 2u;
    rx->parity_valid = fault != 3u;
    if (exchanges == 2u && fault == 4u)
    {
        rx->data[4] ^= 1u; /* Valid framing, invalid anticollision BCC. */
    }
    if (exchanges == 3u && fault == 5u)
    {
        rx->data[1] ^= 1u; /* Corrupt the SAK's CRC. */
    }
    if (exchanges == 3u && fault == 6u)
    {
        rx->data[0] ^= 4u;
        nfca_append_crc(rx->data, 1u); /* Inconsistent cascade marker. */
    }
    return true;
}

size_t host_nfca_encode(const uint8_t *data,
                        uint8_t size,
                        uint8_t final_bits,
                        uint8_t *slots,
                        size_t capacity)
{
    reader_frame_t frame;
    reader_tx_waveform_t waveform;
    if (!reader_frame_begin(&frame, size))
    {
        return 0u;
    }
    memcpy(frame.data, data, size);
    frame.last_byte_bits = final_bits;
    frame.byte_parity = final_bits == 0u;
    if (!nfca_pcd_encode(&frame, &waveform))
    {
        return 0u;
    }
    size_t used = 0u;
    for (uint16_t i = 0u; i < waveform.count; i++)
    {
        const reader_tx_segment_t *segment = &waveform.segments[i];
        for (uint16_t fc = 0u; fc < segment->duration_fc; fc += 32u)
        {
            if (used == capacity)
            {
                return 0u;
            }
            slots[used++] = segment->level;
        }
    }
    return used;
}

uint32_t get_rand_32(void)
{
    assert(!host_mifare_interrupts_masked());
    return 0x12345678u;
}

nfc_status_t host_nfca_load_view(const card_image_t *image, uint8_t error)
{
    if (image_validate(image) != IMAGE_OK)
    {
        return NFC_INVALID_IMAGE;
    }
    (void)nfca_reader_stop();
    const bool classic = card_model_is_classic(image->model);
    card_peer = classic ? &nfca_mifare_profile : &nfca_ntag_profile;
    if (!(classic ? nfca_mifare_load_image(image)
                  : nfca_ntag_load_image(image)))
    {
        return NFC_INVALID_IMAGE;
    }
    fault = error;
    exchanges = 0u;
    page_writes = 0u;
    classic_writes = selections = authentications = 0u;
    password_authentications = field_stops = 0u;
    return NFC_OK;
}

bool nfca_pcd_exchange_timed(const reader_frame_t *tx,
                             nfca_pcd_response_t *rx,
                             uint16_t max_bits,
                             uint32_t sof_timeout_us)
{
    (void)sof_timeout_us;
    return nfca_pcd_exchange(tx, rx, max_bits);
}
