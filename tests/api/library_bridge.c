#include "board/board_config.h"
#include "cards/mifare/reader/mifare_reader.h"
#include "cards/type2/reader/ntag_reader.h"
#include "emulation/emulation.h"
#include "phy/nfcv/pcd/nfcv_pcd_rx.h"
#include "phy/nfcv/pcd/nfcv_pcd_tx.h"
#include "pn532/pn532_frontend.h"
#include "pn532/pn532_transport.h"
#include "pn532_unleashed.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/reader.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/pwm.h"
#include "hardware/sync.h"

#include <string.h>

static uint32_t clock_hz = PNU_REQUIRED_SYS_CLOCK_HZ;
static bool init_failure, stop_failure, start_failure;
static uint8_t model = 1u, fault, failed_block;
static uint8_t command, block;
static uint32_t init_calls, start_calls, field_calls, read_calls, releases;
static uint8_t claimed_sms, irq_conflicts;
static nfca_pcd_response_t injected;
static nfc_status_t injected_status;
static bool inject_nfcv;
static bool receive_armed;
static uint32_t field_off_calls;
static uint32_t exchange_calls, timeout_seen_us;
static reader_frame_t last_tx;
static nfc_config_t frontend_pins;
static nfc_transport_t selected_transport;

void pn532_transport_select(nfc_transport_t transport)
{
    selected_transport = transport;
}

uint8_t host_selected_transport(void)
{
    return (uint8_t)selected_transport;
}

void host_library_pins(nfc_config_t *pins)
{
    *pins = frontend_pins;
}

void host_public_reply(const uint8_t *data,
                       uint16_t bits,
                       bool parity_valid,
                       nfc_status_t status,
                       bool nfcv)
{
    injected =
        (nfca_pcd_response_t){.size_bits = bits, .parity_valid = parity_valid};
    if (data != NULL && bits <= sizeof(injected.data) * 8u)
    {
        memcpy(injected.data, data, (bits + 7u) / 8u);
    }
    injected_status = status;
    inject_nfcv = nfcv;
}

uint32_t host_public_count(void)
{
    return exchange_calls;
}

uint32_t host_public_timeout(void)
{
    return timeout_seen_us;
}

uint16_t host_public_tx(uint8_t *bytes)
{
    memcpy(bytes, last_tx.data, last_tx.length);
    return (uint16_t)(last_tx.length * 8u - (last_tx.last_byte_bits != 0u
                                                 ? 8u - last_tx.last_byte_bits
                                                 : 0u));
}
static pwm_hw_t pwm_state;
pwm_hw_t *pwm_hw = &pwm_state;

void host_resources_busy(uint8_t mask)
{
    claimed_sms = mask & 15u;
    irq_conflicts = mask & 0x30u;
    pwm_state.slice[0].csr = mask & 0x40u ? PWM_CH0_CSR_EN_BITS : 0u;
}

uint8_t host_resources_claimed(void)
{
    return claimed_sms;
}

bool pio_sm_is_claimed(PIO pio, unsigned int sm)
{
    return (claimed_sms & (1u << (sm + (pio == pio0 ? 0u : 2u)))) != 0u;
}

void pio_sm_claim(PIO pio, unsigned int sm)
{
    claimed_sms |= (uint8_t)(1u << (sm + (pio == pio0 ? 0u : 2u)));
}

bool irq_has_shared_handler(unsigned int irq)
{
    (void)irq;
    return (irq_conflicts & 0x10u) != 0u;
}

static void other_irq_handler(void)
{
}

irq_handler_t irq_get_exclusive_handler(unsigned int irq)
{
    (void)irq;
    return irq_conflicts & 0x20u ? other_irq_handler : NULL;
}

void host_library_clock(uint32_t hz)
{
    clock_hz = hz;
}

void host_library_fail(bool init, bool stop, bool start)
{
    init_failure = init;
    stop_failure = stop;
    start_failure = start;
}

void host_reader_config(uint8_t card_model, uint8_t failure, uint8_t index)
{
    model = card_model;
    fault = failure;
    failed_block = index;
}

uint32_t host_library_count(uint8_t counter)
{
    switch (counter)
    {
    case 0:
        return init_calls;
    case 1:
        return start_calls;
    case 2:
        return field_calls;
    case 3:
        return read_calls;
    case 5:
        return field_off_calls;
    default:
        return releases;
    }
}

uint32_t clock_get_hz(enum clock_index clock)
{
    (void)clock;
    return clock_hz;
}

bool pn532_frontend_init(void)
{
    init_calls++;
    frontend_pins = board_config.pins;
    return !init_failure;
}

bool pn532_frontend_stop(void)
{
    return !stop_failure;
}

bool pn532_frontend_prepare_reader(uint8_t protocol)
{
    return protocol == 3u && !init_failure;
}

bool pn532_frontend_set_field(bool enabled)
{
    receive_armed = enabled && !stop_failure;
    field_off_calls += !enabled;
    field_calls++;
    return !stop_failure;
}

bool emulation_load(const card_image_t *image)
{
    (void)image;
    start_calls++;
    return !start_failure;
}

bool emulation_stop(void)
{
    return !stop_failure;
}

bool nfcv_pcd_tx_init(void)
{
    return fault != 6u;
}

bool nfcv_pcd_rx_init(void)
{
    return fault != 7u;
}

void reader_subcarrier_level_rx_release(void)
{
    releases++;
}

void pn532_reader_sequence_release(void)
{
    releases++;
}

bool nfcv_pcd_tx_send(const reader_frame_t *frame)
{
    last_tx = *frame;
    exchange_calls++;
    if (!nfcv_check_crc(frame->data, frame->length))
    {
        return false;
    }
    command = frame->data[1];
    block = frame->data[10];
    return true;
}

bool nfcv_pcd_rx_capture(nfcv_pcd_rx_result_t *rx)
{
    static const uint8_t uid[] = {5u, 4u, 3u, 2u, 1u, 1u, 4u, 0xe0u};
    rx->sof_seen = rx->eof_seen = rx->crc_ok = rx->framing_valid = true;
    rx->bytes[0] = 0u;
    if (fault == 1u)
    {
        memset(rx, 0, sizeof(*rx));
        return false;
    }
    if (command == 1u || command == 0x2bu)
    {
        rx->bytes[1] = command == 1u ? 0x12u : 0x0fu;
        memcpy(&rx->bytes[2], uid, sizeof(uid));
        rx->bytes[7] = model;
        rx->size_bytes = command == 1u ? 12u : 17u;
        if (command == 0x2bu)
        {
            rx->bytes[10] = 0x12u;
            rx->bytes[11] = 0x34u;
            rx->bytes[12] = model == 1u ? 27u : 7u;
            rx->bytes[13] = 3u;
            rx->bytes[14] = 0x56u;
            if (fault == 2u)
            {
                rx->bytes[2] ^= 1u;
            }
        }
    }
    else if (command == 0x20u)
    {
        read_calls++;
        if (fault == 3u && block == failed_block)
        {
            memset(rx, 0, sizeof(*rx));
            return false;
        }
        rx->size_bytes = 8u;
        rx->bytes[1] = fault == 5u ? 2u : block % 2u;
        for (uint8_t i = 0u; i < 4u; i++)
        {
            rx->bytes[2u + i] = block * 4u + i;
        }
    }
    else
    {
        return false;
    }
    nfcv_append_crc(rx->bytes, rx->size_bytes - 2u);
    if (fault == 4u)
    {
        rx->crc_ok = false;
    }
    return true;
}

uint32_t save_and_disable_interrupts(void)
{
    return 0u;
}

void restore_interrupts(uint32_t state)
{
    (void)state;
}

void sleep_us(uint64_t microseconds)
{
    (void)microseconds;
}

bool nfca_reader_stop(void)
{
    return true;
}
nfc_status_t nfca_reader_scan_start(nfca_tag_t *tag)
{
    return nfca_reader_open_selected(tag);
}

nfc_status_t nfca_reader_open_selected(nfca_tag_t *tag)
{
    if (fault == 9u)
    {
        *tag = (nfca_tag_t){.uid_size_bytes = 4u};
        return NFC_OK;
    }
    return NFC_NO_TAG;
}

nfc_status_t ntag_reader_read(const ntag_config_t *config,
                              const uint8_t *password,
                              nfca_dump_t *dump)
{
    (void)config;
    (void)password;
    dump->memory_size_bytes = 540u;
    return fault == 1u ? NFC_NO_TAG : NFC_OK;
}

/* Recovery core is substituted only in the public role/ownership tests. */
void mifare_recovery_reset(void)
{
}
void mifare_access_reader_reset(void)
{
}
void mifare_access_reader_selected(void)
{
}
void ntag_access_reader_reset(void)
{
}
nfc_status_t mifare_access_reader(uint8_t block,
                                  bool key_b,
                                  const uint8_t key[6],
                                  const uint8_t *before,
                                  const uint8_t *after,
                                  uint8_t data[16],
                                  bool *writable)
{
    (void)block;
    (void)key_b;
    (void)key;
    (void)before;
    (void)after;
    (void)data;
    (void)writable;
    return NFC_UNSUPPORTED;
}
nfc_status_t ntag_access_reader(uint8_t page,
                                const uint8_t *password,
                                const uint8_t *before,
                                const uint8_t *after,
                                uint8_t data[4],
                                bool *writable)
{
    (void)page;
    (void)password;
    (void)before;
    (void)after;
    (void)data;
    (void)writable;
    return NFC_UNSUPPORTED;
}
nfc_status_t nfca_reader_restart(const nfca_tag_t *expected)
{
    (void)expected;
    return NFC_OK;
}

/* The peer supplies capture metadata; timed capture preserves CRC observations.
 */
bool nfcv_pcd_rx_capture_timed(nfcv_pcd_rx_result_t *rx,
                               uint32_t sof_timeout_us)
{
    timeout_seen_us = sof_timeout_us;
    /* Model the PN532 decoder leaving Receive after an RF frame. A second
     * exchange must explicitly rearm it while keeping the field active. */
    if (!receive_armed)
    {
        *rx = (nfcv_pcd_rx_result_t){0};
        return false;
    }
    receive_armed = false;
    if (inject_nfcv)
    {
        *rx = (nfcv_pcd_rx_result_t){0};
        if (injected_status != NFC_OK)
        {
            rx->sof_seen = injected_status == NFC_INVALID_RESPONSE;
            return false;
        }
        rx->size_bytes = (uint8_t)(injected.size_bits / 8u);
        memcpy(rx->bytes, injected.data, rx->size_bytes);
        rx->sof_seen = rx->eof_seen = rx->framing_valid = true;
        rx->crc_ok = nfcv_check_crc(rx->bytes, rx->size_bytes);
        return true;
    }
    (void)nfcv_pcd_rx_capture(rx);
    return rx->framing_valid && rx->eof_seen && !rx->truncated;
}

bool emulation_start_nfca(nfca_handler_t handler, void *context)
{
    (void)handler;
    (void)context;
    start_calls++;
    return !start_failure;
}

bool emulation_start_nfcv(nfcv_handler_t handler, void *context)
{
    (void)handler;
    (void)context;
    start_calls++;
    return !start_failure;
}

nfc_status_t nfca_reader_open(void)
{
    start_calls++;
    return start_failure ? NFC_RF_ERROR : NFC_OK;
}

nfc_status_t nfca_reader_select_tag(nfca_tag_t *tag)
{
    (void)tag;
    return NFC_NO_TAG;
}

nfc_status_t nfca_reader_transfer_frame(const reader_frame_t *tx,
                                        nfca_pcd_response_t *rx,
                                        uint32_t sof_timeout_us,
                                        bool expect_response)
{
    last_tx = *tx;
    exchange_calls++;
    timeout_seen_us = sof_timeout_us;
    *rx = injected;
    return expect_response ? injected_status : NFC_OK;
}

void host_public_crc(uint8_t *bytes, size_t size_bytes, bool nfcv)
{
    if (nfcv)
    {
        nfcv_append_crc(bytes, size_bytes);
    }
    else
    {
        nfca_append_crc(bytes, size_bytes);
    }
}
