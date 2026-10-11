#include "phy/nfca/picc/nfca_tx.h"

#include "board/tag_board.h"
#include "phy/nfca/picc/nfca_trace.h"
#include "phy/picc/picc_tx_stream.h"
#include "pico.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/tag.h"

#include "hardware/structs/pwm.h"
#include "pico/stdlib.h"

#define PWM_TIMER_SLICE NFCA_TIMER_SLICE
#define PWM_TIMER_IRQ_MASK (1u << PWM_TIMER_SLICE)
#define PWM_TIMER_TOP_TAG_RSP UINT16_MAX

#define FDT_TAG_BASE NFCA_TAG_FDT_BASE_CYCLES
#define FDT_BIT_PERIOD NFC_128FC_SYS_CYCLES
#define FDT_MAX_PERIODS 8u
#define FDT_LAST_ZERO_ADVANCE_FC 64u
#if NFC_SYS_CLOCKS_PER_FC == 4u
/* PM3 comparison shows 16/fc extra EOF-to-launch overhead at 54.24 MHz.
 * Account for it in the stopwatch target, keeping the physical FDT slot. */
#define FDT_START_COMPENSATION_CYCLES NFC_FC_TO_SYS_CYCLES(16u)
#else
#define FDT_START_COMPENSATION_CYCLES 0u
#endif
/* Manchester stream symbols: D is SOF/one, E is zero, and F terminates. */
#define NFCA_TX_D_PATTERN 0x0055u
#define NFCA_TX_E_PATTERN 0x5500u
#define NFCA_TX_F_PATTERN 0x0000u

static bool fdt_last_bit = true;
static uint32_t fdt_base_cycles = FDT_TAG_BASE;

void __not_in_flash_func(nfca_tx_set_fdt_base)(uint32_t base_cycles)
{
    fdt_base_cycles = (base_cycles != 0u) ? base_cycles : FDT_TAG_BASE;
}

void __not_in_flash_func(nfca_tx_fdt_set_last_bit)(bool last_bit_1)
{
    fdt_last_bit = last_bit_1;
}

static __attribute__((noinline)) bool
__not_in_flash_func(start_on_fdt_grid)(void)
{
    const uint32_t base_delay =
        (fdt_last_bit ? fdt_base_cycles
                      : (fdt_base_cycles -
                         NFC_FC_TO_SYS_CYCLES(FDT_LAST_ZERO_ADVANCE_FC))) -
        FDT_START_COMPENSATION_CYCLES;
    uint32_t deadline = base_delay;

    /* Keep the RX stopwatch running. Stopping it while selecting a slot
     * adds CPU-dependent time outside the measured FDT. TOP remains 65535;
     * normal response deadlines are well below one wrap. */
    if ((pwm_hw->intr & PWM_TIMER_IRQ_MASK) != 0u)
    {
        tag_tx_stream_start();
        NFCA_TRACE_TX((uint16_t)pwm_hw->slice[PWM_TIMER_SLICE].ctr, true, true);
        return false;
    }
    for (uint8_t n = 0u; n <= FDT_MAX_PERIODS; n++)
    {
        if (pwm_hw->slice[PWM_TIMER_SLICE].ctr < deadline)
        {
            while (pwm_hw->slice[PWM_TIMER_SLICE].ctr < deadline)
            {
                tight_loop_contents();
            }
            tag_tx_stream_start();
            NFCA_TRACE_TX(
                (uint16_t)pwm_hw->slice[PWM_TIMER_SLICE].ctr, false, false);
            return true;
        }
        deadline += FDT_BIT_PERIOD;
    }
    /* Preserve the existing late-response policy for variable-latency
     * profiles: transmit even when this FDT grid could not be met. */
    tag_tx_stream_start();
    NFCA_TRACE_TX((uint16_t)pwm_hw->slice[PWM_TIMER_SLICE].ctr, true, false);
    return false;
}

static __force_inline void send_d(void)
{
    tag_tx_stream_put(NFCA_TX_D_PATTERN);
}

static __force_inline void send_e(void)
{
    tag_tx_stream_put(NFCA_TX_E_PATTERN);
}

static __force_inline void send_f(void)
{
    NFCA_TRACE_UNDERRUN(tag_tx_stream_underrun());
    tag_tx_stream_put(NFCA_TX_F_PATTERN);
}

static __force_inline void
send_bits_with_parity(uint8_t value, bool parity, uint8_t count)
{
    for (uint8_t bit = 0u; bit < count; bit++)
    {
        if ((value & 1u) != 0u)
        {
            send_d();
        }
        else
        {
            send_e();
        }
        value >>= 1u;
    }

    if (parity)
    {
        send_d();
    }
    else
    {
        send_e();
    }
}

static __force_inline void send_byte_with_parity(uint8_t value, bool parity)
{
    send_bits_with_parity(value, parity, 8u);
}

void __not_in_flash_func(nfca_tx_fdt_prepare)(void)
{
    /* Configuration belongs before capture, outside the EOF-to-timer path. */
    pwm_hw->slice[PWM_TIMER_SLICE].csr = 0;
    pwm_hw->intr = PWM_TIMER_IRQ_MASK;
    pwm_hw->slice[PWM_TIMER_SLICE].top = PWM_TIMER_TOP_TAG_RSP;
}

void __not_in_flash_func(nfca_tx_fdt_start_elapsed)(uint16_t elapsed_cycles)
{
    pwm_hw->slice[PWM_TIMER_SLICE].ctr = elapsed_cycles;
    pwm_hw->slice[PWM_TIMER_SLICE].csr = PWM_CH0_CSR_EN_BITS;
}

static bool __not_in_flash_func(nfca_tx_send4bit_fdt)(uint8_t data4bit)
{
    tag_tx_stream_prepare();
    send_d();
    const bool waited = start_on_fdt_grid();
    for (uint8_t i = 0; i < 4u; i++)
    {
        if (data4bit & 1u)
        {
            send_d();
        }
        else
        {
            send_e();
        }
        data4bit >>= 1;
    }
    send_f();
    tag_tx_stream_finish();
    return waited;
}

static bool __not_in_flash_func(nfca_tx_send_data_fdt)(const uint8_t *resp,
                                                       uint16_t len)
{
    uint8_t parity[NFCA_TAG_MAX_RESPONSE_BYTES];
    for (uint16_t i = 0u; i < len; i++)
    {
        parity[i] = nfca_odd_parity_bit(resp[i]) ? 1u : 0u;
    }
    tag_tx_stream_prepare();
    send_d();
    const bool waited = start_on_fdt_grid();
    for (uint16_t i = 0; i < len; i++)
    {
        send_byte_with_parity(resp[i], parity[i] != 0u);
    }
    send_f();
    tag_tx_stream_finish();
    return waited;
}

static bool __not_in_flash_func(nfca_tx_send_data_parity_fdt)(
    const uint8_t *resp, const uint8_t *parity, uint16_t len)
{
    tag_tx_stream_prepare();
    send_d();
    const bool waited = start_on_fdt_grid();
    for (uint16_t i = 0u; i < len; i++)
    {
        send_byte_with_parity(resp[i], parity[i] != 0u);
    }
    send_f();
    tag_tx_stream_finish();
    return waited;
}

static void
__not_in_flash_func(send_deferred_parity)(nfca_tag_response_t *response)
{
    tag_tx_stream_prepare();
    send_d();
    if (response->frame.bytes[0] & 1u)
    {
        send_d();
    }
    else
    {
        send_e();
    }
    (void)start_on_fdt_grid();
    response->prepare_bytes(response, 0u, 1u);
    send_bits_with_parity(
        response->frame.bytes[0] >> 1u, response->frame.parity[0] != 0u, 7u);
    /* The joined FIFO now holds the remaining first-byte symbols. */
    response->prepare_bytes(response, 1u, response->frame.size_bytes - 1u);
    for (uint16_t i = 1u; i < response->frame.size_bytes; i++)
    {
        send_byte_with_parity(response->frame.bytes[i],
                              response->frame.parity[i] != 0u);
    }
    send_f();
    tag_tx_stream_finish();
}

static void
__not_in_flash_func(nfca_tx_send_response_fdt)(nfca_tag_response_t *response)
{
    switch ((unsigned int)response->frame.type)
    {
    case NFCA_TAG_RESPONSE_DATA:
        (void)nfca_tx_send_data_fdt(response->frame.bytes,
                                    response->frame.size_bytes);
        break;
    case NFCA_TAG_RESPONSE_DATA_WITH_PARITY:
        (void)nfca_tx_send_data_parity_fdt(response->frame.bytes,
                                           response->frame.parity,
                                           response->frame.size_bytes);
        break;
    case NFCA_TAG_RESPONSE_DEFERRED_PARITY:
        send_deferred_parity(response);
        break;
    case NFCA_TAG_RESPONSE_SHORT4:
        (void)nfca_tx_send4bit_fdt(response->frame.short4);
        break;
    case NFCA_TAG_RESPONSE_NONE:
        break;
    }
}

void __not_in_flash_func(nfca_tx_send_response)(nfca_tag_response_t *response)
{
    nfca_tx_send_response_fdt(response);
}
