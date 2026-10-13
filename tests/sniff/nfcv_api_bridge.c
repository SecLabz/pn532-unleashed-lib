/* Exercise the production sniff API with deterministic peripheral failures. */
#include "api_internal.h"
#include "phy/nfcv/sniff/sniff_internal.h"
#include "pn532/pn532_command.h"
#include "pn532/pn532_frontend.h"
#include "pn532/pn532_registers.h"

static nfc_state_t state = NFC_STATE_IDLE;
static uint8_t registers[3] = {0x18u, 0x20u, 0xbbu};
static unsigned calls;
static unsigned words;
static bool capture_failure;
static bool write_failure;
static bool read_failure;
static bool overrun;
static bool captured;

nfc_state_t nfc_state(void)
{
    return state;
}
nfc_status_t nfc_operation_begin(nfc_state_t next)
{
    if (state != NFC_STATE_IDLE)
    {
        return state == NFC_STATE_ERROR ? NFC_RF_ERROR : NFC_BUSY;
    }
    state = next;
    return NFC_OK;
}
void nfc_operation_end(nfc_status_t status)
{
    state = status == NFC_RF_ERROR ? NFC_STATE_ERROR : NFC_STATE_IDLE;
}

static unsigned register_index(uint16_t address)
{
    return address == CIU_TEST_SEL1 ? 0u : address == CIU_TEST_SEL2 ? 1u : 2u;
}
bool pn532_read_register(uint16_t address, uint8_t *value)
{
    calls++;
    if (read_failure)
    {
        return false;
    }
    *value = registers[register_index(address)];
    return true;
}
bool pn532_write_register(uint16_t address, uint8_t value)
{
    calls++;
    if (write_failure)
    {
        return false;
    }
    registers[register_index(address)] = value;
    return true;
}
bool pn532_frontend_prepare_nfcv_sniff(void)
{
    calls++;
    return true;
}
bool pn532_frontend_stop(void)
{
    calls++;
    return true;
}
bool sniff_capture_start(const uint8_t *pins, uint8_t channels)
{
    (void)pins;
    (void)channels;
    captured = !capture_failure;
    overrun = false;
    return captured;
}
void sniff_capture_stop(void)
{
    captured = false;
}
void sniff_capture_pause(void)
{
}
bool sniff_capture_read(uint32_t levels[3])
{
    if (!captured || overrun || words == 0u)
    {
        return false;
    }
    words--;
    levels[0] = 0u;
    return true;
}
bool sniff_capture_overrun(void)
{
    return overrun;
}
void nfcv_sniff_decode_reset(void)
{
}
void nfcv_sniff_decode_stop(void)
{
}
void nfcv_sniff_decode_word(const uint32_t levels[3])
{
    (void)levels;
    const nfcv_sniff_frame_t frame = {
        .size_bits = 8u,
        .size_bytes = 1u,
        .direction = NFCV_SNIFF_PCD,
        .flags = NFCV_SNIFF_DECODED,
        .data = {0x52u},
    };
    nfcv_sniff_publish(&frame);
}

void host_sniff_api_reset(void)
{
    write_failure = false;
    read_failure = false;
    (void)nfcv_sniff_stop();
    state = NFC_STATE_IDLE;
    calls = 0u;
    words = 0u;
    capture_failure = false;
}
unsigned host_sniff_api_calls(void)
{
    return calls;
}
void host_sniff_api_capture_failure(bool fail)
{
    capture_failure = fail;
}
void host_sniff_api_write_failure(bool fail)
{
    write_failure = fail;
}
void host_sniff_api_words(unsigned count)
{
    words = count;
}
void host_sniff_api_overrun(void)
{
    overrun = true;
}
uint8_t host_sniff_api_register(unsigned index)
{
    return registers[index];
}

void host_sniff_api_read_failure(bool fail)
{
    read_failure = fail;
}
