#include "phy/pcd/reader_rf_trace.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

static struct
{
    uint16_t step_index;
    uint16_t size_bits;
    uint8_t stage;
    uint8_t select_phase;
    uint8_t reason;
    uint8_t status;
    bool sent;
    bool framing_valid;
    bool parity_valid;
    bool collision;
} trace;

static struct
{
    uint32_t phases_us[6];
    uint32_t exchange_us[5];
    uint16_t exchange_count;
    bool valid;
    bool active;
} collect;

bool __not_in_flash_func(reader_rf_trace_collect_active)(void)
{
    return collect.active;
}

void reader_rf_trace_exchange(const uint32_t marks_us[4],
                              uint32_t capture_us,
                              uint32_t first_edge_us)
{
    collect.exchange_us[0] += marks_us[1] - marks_us[0];
    collect.exchange_us[1] += marks_us[2] - marks_us[1];
    collect.exchange_us[2] += capture_us;
    collect.exchange_us[3] += marks_us[3] - marks_us[2];
    collect.exchange_us[4] += first_edge_us;
    collect.exchange_count++;
}

void reader_rf_trace_collect_begin(bool enabled)
{
    memset(&collect, 0, sizeof(collect));
    collect.active = enabled;
}

void reader_rf_trace_collect(const uint32_t marks_us[7])
{
    collect.valid = true;
    collect.active = false;
    for (uint8_t phase = 0u; phase < 6u; phase++)
    {
        collect.phases_us[phase] = marks_us[phase + 1u] - marks_us[phase];
    }
}

void reader_rf_trace_reset(void)
{
    memset(&trace, 0, sizeof(trace));
    memset(&collect, 0, sizeof(collect));
}

void reader_rf_trace_step(uint16_t index)
{
    trace.step_index = index;
}

void __not_in_flash_func(reader_rf_trace_stage)(
    enum reader_rf_trace_stage stage)
{
    trace.stage = stage;
    trace.select_phase = READER_TRACE_SELECT_NONE;
    trace.reason = READER_TRACE_REASON_NONE;
    trace.sent = false;
    trace.size_bits = 0u;
    trace.framing_valid = false;
    trace.parity_valid = false;
    trace.collision = false;
}

void reader_rf_trace_select(enum reader_rf_trace_select phase)
{
    trace.select_phase = phase;
}

void __not_in_flash_func(reader_rf_trace_reason)(
    enum reader_rf_trace_reason reason)
{
    trace.reason = reason;
}

void __not_in_flash_func(reader_rf_trace_response)(bool sent,
                                                   uint16_t bit_count,
                                                   bool framing_valid,
                                                   bool parity_valid,
                                                   bool collision)
{
    trace.sent = sent;
    trace.size_bits = bit_count;
    trace.framing_valid = framing_valid;
    trace.parity_valid = parity_valid;
    trace.collision = collision;
}

void nfc_trace_finish(uint8_t status)
{
    trace.status = status;
}

uint16_t nfc_trace_reader(char *output, uint16_t capacity)
{
    const int count =
        snprintf(output,
                 capacity,
                 "reader_status=step:%u,stage:%u,select:%u,status:%u,sent:%u,"
                 "bits:%u,framing:%u,parity_valid:%u,collision:%u,reason:%u\n",
                 trace.step_index,
                 trace.stage,
                 trace.select_phase,
                 trace.status,
                 trace.sent,
                 trace.size_bits,
                 trace.framing_valid,
                 trace.parity_valid,
                 trace.collision,
                 trace.reason);
    if (count <= 0 || count >= capacity)
    {
        return 0u;
    }
    if (!collect.valid)
    {
        return (uint16_t)count;
    }
    uint32_t total_us = 0u;
    for (uint8_t phase = 0u; phase < 6u; phase++)
    {
        total_us += collect.phases_us[phase];
    }
    const int extra = snprintf(output + count,
                               capacity - count,
                               "reader_collect_us=prepare:%lu,select:%lu,"
                               "entropy:%lu,auth:%lu,target:%lu,expire:%lu,"
                               "total:%lu\n",
                               (unsigned long)collect.phases_us[0],
                               (unsigned long)collect.phases_us[1],
                               (unsigned long)collect.phases_us[2],
                               (unsigned long)collect.phases_us[3],
                               (unsigned long)collect.phases_us[4],
                               (unsigned long)collect.phases_us[5],
                               (unsigned long)total_us);
    if (extra <= 0 || extra >= capacity - count)
    {
        return 0u;
    }
    const uint16_t used = (uint16_t)(count + extra);
    const int exchange = snprintf(output + used,
                                  capacity - used,
                                  "reader_exchange_us=encode:%lu,tx_rx:%lu,"
                                  "rx:%lu,decode:%lu,first_edge:%lu,calls:%u\n",
                                  (unsigned long)collect.exchange_us[0],
                                  (unsigned long)collect.exchange_us[1],
                                  (unsigned long)collect.exchange_us[2],
                                  (unsigned long)collect.exchange_us[3],
                                  (unsigned long)collect.exchange_us[4],
                                  collect.exchange_count);
    return exchange > 0 && exchange < capacity - used
               ? (uint16_t)(used + exchange)
               : 0u;
}
