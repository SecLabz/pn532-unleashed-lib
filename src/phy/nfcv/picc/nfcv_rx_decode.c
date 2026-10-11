#include "phy/nfcv/picc/nfcv_rx_decode.h"

#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico.h"
#else
#define __not_in_flash_func(name) name
#endif

bool __not_in_flash_func(nfcv_1of256_decoder_finish)(
    const nfcv_1of256_decoder_t *decoder, nfcv_frame_t *frame)
{
    if (decoder == NULL || frame == NULL)
    {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->size_bytes = decoder->size_bytes;
    if (decoder->size_bytes != 0u)
    {
        memcpy(frame->bytes, decoder->bytes, decoder->size_bytes);
        frame->high_data_rate =
            (frame->bytes[0] & NFCV_REQ_DATARATE_HIGH) != 0u;
        frame->single_subcarrier =
            (frame->bytes[0] & NFCV_REQ_SUBCARRIER_TWO) == 0u;
    }

    const bool framing_valid = !decoder->invalid && decoder->eof_seen &&
                               decoder->sample_index == 0u &&
                               !decoder->pause_seen;
    frame->crc_ok = framing_valid &&
                    decoder->size_bytes >= NFCV_REQUEST_MIN_BYTES &&
                    nfcv_check_crc(frame->bytes, frame->size_bytes);
    return frame->crc_ok;
}

bool __not_in_flash_func(nfcv_rx_decode_samples)(const uint8_t *samples,
                                                 uint16_t sample_count,
                                                 nfcv_frame_t *frame)
{
    if (samples == NULL || frame == NULL ||
        sample_count < NFCV_CAPTURE_MIN_SAMPLES)
    {
        return false;
    }
    memset(frame, 0, sizeof(*frame));
    if (!nfcv_sample_is_sof(samples[0]))
    {
        return false;
    }

    uint8_t data_pairs = 0u;
    uint8_t byte_acc = 0u;
    for (uint16_t i = 1u; i < sample_count; i++)
    {
        if (nfcv_sample_is_eof(samples[i]))
        {
            if (data_pairs != 0u || i + 1u != sample_count)
            {
                return false;
            }
            frame->crc_ok = nfcv_check_crc(frame->bytes, frame->size_bytes);
            if (frame->size_bytes > 0u)
            {
                frame->high_data_rate =
                    (frame->bytes[0] & NFCV_REQ_DATARATE_HIGH) != 0u;
                frame->single_subcarrier =
                    (frame->bytes[0] & NFCV_REQ_SUBCARRIER_TWO) == 0u;
            }
            return frame->size_bytes >= NFCV_REQUEST_MIN_BYTES && frame->crc_ok;
        }

        uint8_t two_bits = 0u;
        if (!nfcv_decode_1of4_sample(samples[i], &two_bits))
        {
            return false;
        }
        byte_acc >>= 2;
        byte_acc |= (uint8_t)(two_bits << 6);
        data_pairs++;
        if (data_pairs == NFCV_1OF4_SYMBOLS_PER_BYTE)
        {
            if (frame->size_bytes >= NFCV_FRAME_MAX_BYTES)
            {
                return false;
            }
            frame->bytes[frame->size_bytes++] = byte_acc;
            data_pairs = 0u;
            byte_acc = 0u;
        }
    }
    return false;
}
