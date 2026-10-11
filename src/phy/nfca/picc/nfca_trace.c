#include "phy/nfca/picc/nfca_trace.h"

#include "common/compiler.h"
#include "common/endian.h"
#include "pn532_unleashed/config.h"

#include <string.h>

#define TRACE_CAPACITY 256u
#define TRACE_HEADER_BYTES 48u
#define TRACE_RECORD_BYTES 32u
#define TRACE_REQUEST_PREFIX_BYTES 8u
#define TRACE_RESPONSE_PREFIX_BYTES 4u
#define TRACE_RX_EOF 0x01u
#define TRACE_RX_DECODED 0x02u
#define TRACE_RX_SHORT_FRAME 0x04u
#define TRACE_RX_PARITY_OK 0x08u
#define TRACE_RX_CRC_OK 0x10u
#define TRACE_RX_SYMBOL_ERROR 0x20u
#define TRACE_RX_STALLED 0x40u
#define TRACE_TX_SENT 0x01u
#define TRACE_TX_LATE 0x02u
#define TRACE_TX_UNDERRUN 0x04u
#define TRACE_TX_TIMER_WRAPPED 0x08u
#define NVB_MIN_COMPLETE_BYTES 2u
#define NVB_MAX_ANTICOLLISION_BYTES 6u
#define NVB_BYTE_COUNT_SHIFT 4u
#define NVB_SPARE_BITS_MASK 0x0fu

enum
{
    TRACE_CLOCK_BYTE = 4,
    TRACE_COUNT_BYTE = 8,
    TRACE_CAPACITY_BYTE = 10,
    TRACE_COUNTERS_BYTE = 12,
};
enum
{
    TRACE_TIMESTAMP_BYTE,
    TRACE_SAMPLED_SLOTS_BYTE = 4,
    TRACE_RX_BYTES_BYTE = 6,
    TRACE_RX_FLAGS_BYTE = 8,
    TRACE_TX_BYTES_BYTE = 10,
    TRACE_FDT_CYCLES_BYTE = 12,
    TRACE_TX_FLAGS_BYTE = 14,
    TRACE_RESPONSE_TYPE_BYTE,
    TRACE_REQUEST_BYTE,
    TRACE_RESPONSE_BYTE = TRACE_REQUEST_BYTE + TRACE_REQUEST_PREFIX_BYTES,
    TRACE_RESERVED_BYTE = TRACE_RESPONSE_BYTE + TRACE_RESPONSE_PREFIX_BYTES,
};

typedef struct
{
    uint32_t timestamp_us;
    uint16_t sampled_slots;
    uint16_t rx_bytes;
    uint16_t rx_flags;
    uint16_t tx_bytes;
    uint16_t fdt_cycles;
    uint8_t tx_flags;
    uint8_t response_type;
    uint8_t request[TRACE_REQUEST_PREFIX_BYTES];
    uint8_t response[TRACE_RESPONSE_PREFIX_BYTES];
    uint32_t reserved;
} trace_record_t;

_Static_assert(sizeof(trace_record_t) == TRACE_RECORD_BYTES, "Trace layout");

static trace_record_t records[TRACE_CAPACITY];
static trace_record_t pending;
static uint32_t total;
static uint32_t rejected;
static uint32_t parity_errors;
static uint32_t crc_errors;
static uint32_t rx_stalls;
static uint32_t tx_late;
static uint32_t tx_underruns;
static uint32_t tx_sent;

void nfca_trace_reset(void)
{
    memset(&pending, 0, sizeof(pending));
    total = rejected = parity_errors = crc_errors = 0u;
    rx_stalls = tx_late = tx_underruns = tx_sent = 0u;
}

void RAM_FUNC(nfca_trace_tx)(uint16_t fdt_cycles, bool late, bool wrapped)
{
    pending.fdt_cycles = fdt_cycles;
    pending.tx_flags = TRACE_TX_SENT | (late ? TRACE_TX_LATE : 0u) |
                       (wrapped ? TRACE_TX_TIMER_WRAPPED : 0u);
}

void RAM_FUNC(nfca_trace_underrun)(bool underrun)
{
    pending.tx_flags |= underrun ? TRACE_TX_UNDERRUN : 0u;
}

void RAM_FUNC(nfca_trace_response)(const nfca_tag_response_t *response)
{
    pending.response_type = response->frame.type;
    pending.tx_bytes = response->frame.type == NFCA_TAG_RESPONSE_SHORT4 ||
                               response->frame.type == NFCA_TAG_RESPONSE_NONE
                           ? 0u
                           : response->frame.size_bytes;
    if (response->frame.type == NFCA_TAG_RESPONSE_SHORT4)
    {
        pending.response[0] = response->frame.short4;
    }
    else if (response->frame.type != NFCA_TAG_RESPONSE_NONE)
    {
        const uint16_t count =
            response->frame.size_bytes < sizeof(pending.response)
                ? response->frame.size_bytes
                : sizeof(pending.response);
        memcpy(pending.response, response->frame.bytes, count);
    }
}

void RAM_FUNC(nfca_trace_record)(const nfca_frame_t *frame,
                                 uint32_t timestamp_us,
                                 uint16_t sampled_slots,
                                 bool eof,
                                 bool decoded,
                                 bool symbol_error,
                                 bool rx_stalled)
{
    pending.timestamp_us = timestamp_us;
    pending.sampled_slots = sampled_slots;
    pending.rx_bytes = frame->size_bytes;
    pending.rx_flags =
        (eof ? TRACE_RX_EOF : 0u) | (decoded ? TRACE_RX_DECODED : 0u) |
        (frame->short_frame ? TRACE_RX_SHORT_FRAME : 0u) |
        (frame->parity_ok ? TRACE_RX_PARITY_OK : 0u) |
        (frame->crc_ok ? TRACE_RX_CRC_OK : 0u) |
        (symbol_error && !frame->short_frame ? TRACE_RX_SYMBOL_ERROR : 0u) |
        (rx_stalled ? TRACE_RX_STALLED : 0u);
    const uint16_t count = frame->size_bytes < sizeof(pending.request)
                               ? frame->size_bytes
                               : sizeof(pending.request);
    /* Password verification is recognizable by opcode and lengths alone. */
    if (count != 0u && frame->bytes[0] == NTAG_CMD_PWD_AUTH)
    {
        pending.request[0] = NTAG_CMD_PWD_AUTH;
        memset(pending.response, 0, sizeof(pending.response));
    }
    else
    {
        memcpy(pending.request, frame->bytes, count);
    }
    records[total & (TRACE_CAPACITY - 1u)] = pending;
    total++;
    rejected += !decoded;
    parity_errors += decoded && !frame->short_frame && !frame->parity_ok;
    const bool cascade = frame->bytes[0] == NFCA_CMD_ANTICOLL_CL1 ||
                         frame->bytes[0] == NFCA_CMD_ANTICOLL_CL2 ||
                         frame->bytes[0] == NFCA_CMD_ANTICOLL_CL3;
    const uint8_t nvb = frame->bytes[1];
    const bool anticollision =
        frame->size_bytes >= 2u && cascade &&
        (nvb >> NVB_BYTE_COUNT_SHIFT) >= NVB_MIN_COMPLETE_BYTES &&
        (nvb >> NVB_BYTE_COUNT_SHIFT) <= NVB_MAX_ANTICOLLISION_BYTES &&
        (nvb & NVB_SPARE_BITS_MASK) < 8u;
    /* Partial anticollision carries UID bits, not a trailing CRC. */
    crc_errors +=
        decoded && frame->size_bytes >= 3u && !anticollision && !frame->crc_ok;
    rx_stalls += rx_stalled;
    tx_sent += (pending.tx_flags & TRACE_TX_SENT) != 0u;
    tx_late += (pending.tx_flags & TRACE_TX_LATE) != 0u;
    tx_underruns += (pending.tx_flags & TRACE_TX_UNDERRUN) != 0u;
    memset(&pending, 0, sizeof(pending));
}

uint16_t nfc_trace_nfca(uint16_t offset, uint8_t *output, uint16_t capacity)
{
    const uint16_t count = total < TRACE_CAPACITY ? total : TRACE_CAPACITY;
    if (output == NULL || capacity < TRACE_HEADER_BYTES || offset > count)
    {
        return 0u;
    }
    memcpy(output, "ATR1", 4u);
    write_le32(&output[TRACE_CLOCK_BYTE], PNU_REQUIRED_SYS_CLOCK_HZ);
    write_le16(&output[TRACE_COUNT_BYTE], count);
    write_le16(&output[TRACE_CAPACITY_BYTE], TRACE_CAPACITY);
    const uint32_t counters[] = {total - count,
                                 total,
                                 rejected,
                                 parity_errors,
                                 crc_errors,
                                 rx_stalls,
                                 tx_late,
                                 tx_underruns,
                                 tx_sent};
    for (uint8_t index = 0u; index < sizeof(counters) / sizeof(counters[0]);
         index++)
    {
        write_le32(&output[TRACE_COUNTERS_BYTE + index * sizeof(counters[0])],
                   counters[index]);
    }
    uint16_t size = TRACE_HEADER_BYTES;
    while (offset < count && (uint16_t)(capacity - size) >= TRACE_RECORD_BYTES)
    {
        const trace_record_t *record =
            &records[(total - count + offset) & (TRACE_CAPACITY - 1u)];
        write_le32(&output[size + TRACE_TIMESTAMP_BYTE], record->timestamp_us);
        write_le16(&output[size + TRACE_SAMPLED_SLOTS_BYTE],
                   record->sampled_slots);
        write_le16(&output[size + TRACE_RX_BYTES_BYTE], record->rx_bytes);
        write_le16(&output[size + TRACE_RX_FLAGS_BYTE], record->rx_flags);
        write_le16(&output[size + TRACE_TX_BYTES_BYTE], record->tx_bytes);
        write_le16(&output[size + TRACE_FDT_CYCLES_BYTE], record->fdt_cycles);
        output[size + TRACE_TX_FLAGS_BYTE] = record->tx_flags;
        output[size + TRACE_RESPONSE_TYPE_BYTE] = record->response_type;
        memcpy(&output[size + TRACE_REQUEST_BYTE],
               record->request,
               sizeof(record->request));
        memcpy(&output[size + TRACE_RESPONSE_BYTE],
               record->response,
               sizeof(record->response));
        write_le32(&output[size + TRACE_RESERVED_BYTE], 0u);
        size += TRACE_RECORD_BYTES;
        offset++;
    }
    return size;
}
