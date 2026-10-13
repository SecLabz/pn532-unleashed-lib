#include "phy/nfca/picc/nfca_frame.h"
#include "phy/nfca/sniff/sniff_internal.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

static struct
{
    uint64_t sample_slots;
    uint64_t pcd_start_slots;
    uint64_t picc_start_slots;
    uint32_t pcd_slots;
    uint32_t last_pause_slots;
    uint16_t picc_slots;
    uint16_t wire_bits;
    uint8_t quarters[NFCA_RX_MAX_BITS];
    nfca_sniff_frame_t picc;
    bool previous_pause;
    bool previous_active;
    bool pcd_active;
    bool picc_active;
    bool pcd_truncated;
    bool picc_truncated;
    bool parity_valid;
    bool synchronized;
    uint16_t quiet_slots;
} decoder;

void nfca_sniff_decode_reset(void)
{
    memset(&decoder, 0, sizeof(decoder));
#ifdef PNU_RF_TRACE
    nfca_sniff_trace_reset();
#endif
}

static void finish_pcd(bool interrupted)
{
    nfca_frame_t decoded;
    nfca_frame_stream_t stream;
    nfca_frame_stream_init(&stream, &decoded);
    const uint32_t tail_slots =
        decoder.last_pause_slots + NFCA_RX_EOF_GAP_SLOTS + 2u;
    const uint16_t eof_slots = tail_slots < NFCA_RX_MAX_SLOTS
                                   ? (uint16_t)tail_slots
                                   : NFCA_RX_MAX_SLOTS;
    const bool complete = nfca_frame_stream_finish(
        &stream, decoder.quarters, eof_slots, &decoded);
    /* EOF is zero. Its last pause is either in the final one's accepted
     * quarters or the next zero's first half. Outside this range, the byte
     * parser omitted tail bits or consumed EOF silence as data/parity. */
    const uint32_t data_end_slots =
        NFCA_RX_DATA_START_SLOT + decoded.size_bits * NFCA_RX_BIT_SLOTS;
    const bool tail_complete =
        decoder.last_pause_slots >=
            data_end_slots - 3u * NFCA_RX_BIT_SLOTS / 4u &&
        decoder.last_pause_slots < data_end_slots + NFCA_RX_BIT_SLOTS / 2u;
    nfca_sniff_frame_t frame = {
        .start_fc = decoder.pcd_start_slots * 8u,
        .duration_fc =
            (uint32_t)(decoder.sample_slots - decoder.pcd_start_slots) * 8u,
        .direction = NFCA_SNIFF_PCD,
        .size_bytes = decoded.size_bytes,
        .size_bits =
            decoded.short_frame ? decoded.size_bits : decoded.size_bytes * 8u,
    };
    memcpy(frame.data, decoded.bytes, decoded.size_bytes);
    if (interrupted || decoder.pcd_truncated || !tail_complete)
    {
        frame.flags |= NFCA_SNIFF_TRUNCATED;
    }
    if (complete && (!stream.failed || decoded.short_frame) &&
        frame.flags == 0u)
    {
        frame.flags |= NFCA_SNIFF_DECODED;
    }
    if (!decoded.short_frame && decoded.size_bytes != 0u)
    {
        frame.flags |= NFCA_SNIFF_PARITY_CHECKED;
        if (decoded.parity_ok)
        {
            frame.flags |= NFCA_SNIFF_PARITY_VALID;
        }
    }
    if (tail_complete && decoded.size_bytes > NFC_CRC_BYTES)
    {
        frame.flags |= NFCA_SNIFF_CRC_CHECKED;
        if (decoded.crc_ok)
        {
            frame.flags |= NFCA_SNIFF_CRC_VALID;
        }
    }
    if (frame.size_bits != 0u || !interrupted)
    {
        nfca_sniff_publish(&frame);
    }
    decoder.pcd_active = false;
}

static void finish_picc(bool interrupted)
{
    nfca_sniff_frame_t *frame = &decoder.picc;
    frame->duration_fc = decoder.picc_slots * 8u;
    if (interrupted || decoder.picc_truncated)
    {
        frame->flags |= NFCA_SNIFF_TRUNCATED;
    }
    if (decoder.wire_bits == 4u)
    {
        frame->size_bits = 4u;
        frame->size_bytes = 1u;
        frame->data[0] &= 0x0fu;
    }
    else
    {
        const uint16_t partial = decoder.wire_bits % 9u;
        frame->size_bits = (decoder.wire_bits / 9u) * 8u + partial;
        frame->size_bytes = (frame->size_bits + 7u) / 8u;
        if (decoder.wire_bits >= 9u)
        {
            frame->flags |= NFCA_SNIFF_PARITY_CHECKED;
            if (decoder.parity_valid)
            {
                frame->flags |= NFCA_SNIFF_PARITY_VALID;
            }
        }
    }
    if ((decoder.wire_bits == 4u ||
         (decoder.wire_bits != 0u && decoder.wire_bits % 9u == 0u)) &&
        (frame->flags & NFCA_SNIFF_TRUNCATED) == 0u)
    {
        frame->flags |= NFCA_SNIFF_DECODED;
    }
    if (frame->size_bits % 8u == 0u && frame->size_bytes > NFC_CRC_BYTES)
    {
        frame->flags |= NFCA_SNIFF_CRC_CHECKED;
        const uint16_t length = frame->size_bytes - NFC_CRC_BYTES;
        const uint16_t crc = (uint16_t)frame->data[length] |
                             (uint16_t)frame->data[length + 1u] << 8u;
        if (nfca_crc16(frame->data, length) == crc)
        {
            frame->flags |= NFCA_SNIFF_CRC_VALID;
        }
    }
    /* Native RxActive also pulses on reader modulation. Those windows are
     * suppressed while the envelope owns a PCD frame. No CRC-based repair,
     * phase search or command templates influence physical decoding. */
    if (decoder.wire_bits >= 4u)
    {
        nfca_sniff_publish(frame);
    }
    decoder.picc_active = false;
}

static void picc_bit(bool data)
{
    const uint16_t byte = decoder.wire_bits / 9u;
    const uint8_t bit = decoder.wire_bits % 9u;
    if (byte >= NFCA_SNIFF_MAX_BYTES)
    {
        decoder.picc_truncated = true;
        return;
    }
    if (bit == 8u)
    {
        decoder.parity_valid &=
            data == nfca_odd_parity_bit(decoder.picc.data[byte]);
    }
    else if (data)
    {
        decoder.picc.data[byte] |= (uint8_t)(1u << bit);
    }
    decoder.wire_bits++;
}

static void sample(bool pause, bool data, bool active)
{
    if (!decoder.synchronized)
    {
        decoder.quiet_slots = pause ? 0u : decoder.quiet_slots + 1u;
        if (decoder.quiet_slots >= NFCA_RX_EOF_GAP_SLOTS)
        {
            decoder.synchronized = true;
        }
    }
    if (decoder.synchronized && pause && !decoder.previous_pause &&
        !decoder.pcd_active)
    {
        if (decoder.picc_active)
        {
            finish_picc(true);
        }
        decoder.pcd_active = true;
        decoder.pcd_truncated = false;
        decoder.pcd_start_slots = decoder.sample_slots;
        decoder.pcd_slots = 0u;
        decoder.last_pause_slots = 0u;
        memset(decoder.quarters, 0, sizeof(decoder.quarters));
    }
    if (decoder.pcd_active)
    {
        if (pause && !decoder.previous_pause)
        {
            decoder.last_pause_slots = decoder.pcd_slots;
        }
        if (decoder.pcd_slots < NFCA_RX_MAX_SLOTS)
        {
            if (decoder.pcd_slots >= 2u && pause)
            {
                nfca_quarters_add_level_samples(
                    decoder.quarters, (uint16_t)decoder.pcd_slots, 1u, 1u);
            }
        }
        else
        {
            decoder.pcd_truncated = true;
        }
        decoder.pcd_slots++;
        if (pause && decoder.pcd_slots - decoder.last_pause_slots > 8u)
        {
            /* A reader pause is 32/fc. A sustained low envelope is field
             * loss, not a frame of unbounded duration. Reacquire a quiet
             * carrier before accepting another SOF. */
            finish_pcd(true);
            decoder.synchronized = false;
            decoder.quiet_slots = 0u;
        }
        else if (!pause && decoder.pcd_slots >=
                               decoder.last_pause_slots + NFCA_RX_EOF_GAP_SLOTS)
        {
            finish_pcd(false);
        }
    }
    /* Native tag SOF is independent of reader-envelope synchronization.
     * Weak coupling can leave SIGOUT low while AUX still carries valid tag
     * replies. The physical PCD owner still suppresses reader noise windows. */
    if (active && !decoder.previous_active && !decoder.pcd_active)
    {
        decoder.picc_active = true;
        decoder.picc_truncated = false;
        decoder.picc_start_slots = decoder.sample_slots;
        decoder.picc_slots = 0u;
        decoder.wire_bits = 0u;
        decoder.parity_valid = true;
        decoder.picc = (nfca_sniff_frame_t){
            .direction = NFCA_SNIFF_PICC,
            .start_fc = decoder.sample_slots * 8u,
        };
    }
    if (decoder.picc_active)
    {
        if (!active)
        {
            finish_picc(false);
        }
        else
        {
            if ((decoder.picc_slots & 15u) == 8u)
            {
                picc_bit(data);
            }
            if (decoder.picc_slots != UINT16_MAX)
            {
                decoder.picc_slots++;
            }
            else
            {
                decoder.picc_truncated = true;
            }
        }
    }
    decoder.previous_pause = pause;
    decoder.previous_active = active;
    decoder.sample_slots++;
}

void nfca_sniff_decode_word(const uint32_t levels[3])
{
#ifdef PNU_RF_TRACE
    const uint32_t state = decoder.synchronized |
                           (uint32_t)decoder.pcd_active << 1u |
                           (uint32_t)decoder.picc_active << 2u |
                           (uint32_t)decoder.previous_pause << 3u;
    nfca_sniff_trace_word(levels, state);
#endif
    /* A stable native window has no SOF/EOF or envelope transitions. Only
     * the two fixed bit-center samples can change decoded data. Retain the
     * scalar path at every boundary and near the bounded slot counter limit. */
    const bool stable_envelope =
        (levels[0] == 0u && decoder.previous_pause) ||
        (levels[0] == UINT32_MAX && !decoder.previous_pause);
    if (decoder.picc_active && !decoder.pcd_active && decoder.previous_active &&
        levels[2] == UINT32_MAX && stable_envelope &&
        decoder.picc_slots <= UINT16_MAX - 32u)
    {
        const unsigned first = (8u - decoder.picc_slots) & 15u;
        picc_bit(((levels[1] >> first) & 1u) != 0u);
        picc_bit(((levels[1] >> (first + 16u)) & 1u) != 0u);
        decoder.picc_slots += 32u;
        decoder.sample_slots += 32u;
        if (!decoder.synchronized)
        {
            decoder.quiet_slots =
                decoder.previous_pause ? 0u : decoder.quiet_slots + 32u;
            decoder.synchronized = decoder.quiet_slots >= NFCA_RX_EOF_GAP_SLOTS;
        }
        return;
    }
    if (!decoder.pcd_active && !decoder.picc_active &&
        (levels[0] == UINT32_MAX || levels[0] == 0u) && levels[2] == 0u)
    {
        decoder.sample_slots += 32u;
        decoder.previous_pause = levels[0] == 0u;
        decoder.previous_active = false;
        if (!decoder.synchronized)
        {
            decoder.quiet_slots =
                levels[0] == 0u ? 0u : decoder.quiet_slots + 32u;
            decoder.synchronized = decoder.quiet_slots >= NFCA_RX_EOF_GAP_SLOTS;
        }
        return;
    }
    for (unsigned i = 0u; i < 32u; i++)
    {
        sample(((levels[0] >> i) & 1u) == 0u,
               ((levels[1] >> i) & 1u) != 0u,
               ((levels[2] >> i) & 1u) != 0u);
    }
}

void nfca_sniff_decode_stop(void)
{
    if (decoder.pcd_active)
    {
        finish_pcd(true);
    }
    if (decoder.picc_active)
    {
        finish_picc(true);
    }
}
