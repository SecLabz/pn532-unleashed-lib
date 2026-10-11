#include "pn532/pn532_frontend.h"

#include "board/board_config.h"
#include "pn532/pn532_command.h"
#include "pn532/pn532_registers.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define PN532_FIELD_SETTLE_MS 20u
#define PN532_FIRMWARE_VERSION_BYTES 4u
#define PN532_SAM_TIMEOUT_50_MS_UNITS 0x14u

typedef enum
{
    FRONTEND_IDLE,
    FRONTEND_TAG,
    FRONTEND_READER,
} frontend_role_t;

/* ready implies an armed tag or configured reader; field_enabled belongs
 * only to the reader. A saved register set survives failed restoration
 * until quiet() succeeds, so tag preparation cannot bypass that failure. */
static bool initialized;
static bool ready;
static bool field_enabled;
static frontend_role_t role;
static uint8_t reader_protocol;
static nfca_rx_profile_t nfca_rx_profile = NFCA_RX_Q_33_DB;

bool nfca_reader_set_rx_profile(nfca_rx_profile_t profile)
{
    if (ready || role != FRONTEND_IDLE || field_enabled ||
        (unsigned)profile > NFCA_RX_Q_18_DB)
    {
        return false;
    }
    nfca_rx_profile = profile;
    return true;
}

nfca_rx_profile_t nfca_reader_rx_profile(void)
{
    return nfca_rx_profile;
}

/* Reader-only configuration must not leak into passive tag modes. Capture
 * after quiet(), so restoring these bytes cannot re-enable a carrier. */
static const uint16_t reader_registers[] = {
    CIU_MODE,
    CIU_TX_MODE,
    CIU_RX_MODE,
    CIU_RX_SEL,
    CIU_RX_THRESHOLD,
    CIU_DEMOD,
    CIU_MANUAL_RCV,
    CIU_TX_BIT_PHASE,
    CIU_RF_CFG,
    CIU_GS_N_ON,
    CIU_CW_GS_P,
    CIU_MOD_GS_P,
    CIU_SIC_CLK_EN,
    CIU_CONTROL,
};
static uint8_t
    reader_values[sizeof(reader_registers) / sizeof(reader_registers[0])];
static bool reader_saved;

static bool save_reader_config(void)
{
    for (uint16_t i = 0u; i < sizeof(reader_values); i++)
    {
        if (!pn532_read_register(reader_registers[i], &reader_values[i]))
        {
            return false;
        }
    }
    reader_saved = true;
    return true;
}

static bool restore_reader_config(void)
{
    if (!reader_saved)
    {
        return true;
    }
    for (uint16_t i = 0u; i < sizeof(reader_values); i++)
    {
        if (!pn532_write_register(reader_registers[i], reader_values[i]))
        {
            return false;
        }
    }
    reader_saved = false;
    return true;
}

#ifdef PNU_RF_TRACE
static uint8_t nfca_gsn_off = CIU_GS_N_OFF_MOD_8;
bool pn532_frontend_trace_set_gsn(uint8_t value)
{
    if (ready || role != FRONTEND_IDLE ||
        (value != CIU_GS_N_OFF_MOD_8 && value != CIU_GS_N_OFF_CW_8 &&
         value != CIU_GS_N_OFF_CW_MAX))
    {
        return false;
    }
    nfca_gsn_off = value;
    return true;
}

uint8_t nfc_trace_gsn(void)
{
    return nfca_gsn_off;
}
#endif

static void disconnect_sigin(void)
{
    gpio_init(TAG_LOAD_GPIO);
    gpio_disable_pulls(TAG_LOAD_GPIO);
    gpio_set_dir(TAG_LOAD_GPIO, GPIO_IN);
}

static bool quiet(void)
{
    /* Mask SIGIN before changing roles; clear automatic field activation. */
    bool ok = pn532_update_register(PN532_CONTROL_SWITCH_RNG,
                                    PN532_SVDD_CONTROL_MASK,
                                    PN532_SVDD_CONTROL_MASK);
    ok = pn532_update_register(CIU_TX_SEL, CIU_TX_DRIVER_MASK, 0u) && ok;
    ok = pn532_update_register(CIU_TX_AUTO, CIU_TX_AUTO_FIELD_MASK, 0u) && ok;
    ok = pn532_update_register(CIU_TX_CONTROL, CIU_TX_RF_ENABLE_MASK, 0u) && ok;
    ok = pn532_write_register(CIU_COMMAND, CIU_COMMAND_IDLE) && ok;
    ok = restore_reader_config() && ok;
    return ok;
}

bool pn532_frontend_stop(void)
{
    disconnect_sigin();
    ready = false;
    field_enabled = false;
    role = FRONTEND_IDLE;
    reader_protocol = 0u;
    return !initialized || quiet();
}

static bool failed(void)
{
    pn532_frontend_stop();
    return false;
}

bool pn532_frontend_init(void)
{
    if (initialized)
    {
        return true;
    }
    disconnect_sigin();
    uint8_t version[PN532_FIRMWARE_VERSION_BYTES];
    const uint8_t sam[] = {
        PN532_SAM_NORMAL_MODE, PN532_SAM_TIMEOUT_50_MS_UNITS, 0u};
    if (!pn532_transport_init())
    {
        return false;
    }
    if (pn532_command(PN532_CMD_GET_FIRMWARE_VERSION,
                      NULL,
                      0u,
                      version,
                      sizeof(version)) != (int)sizeof(version) ||
        version[0] != PN532_IC_ID ||
        pn532_command(
            PN532_CMD_SAM_CONFIGURATION, sam, sizeof(sam), NULL, 0u) != 0)
    {
        /* A failed setup must still attempt to stop a previously active
         * PN532. SIGIN has already been disconnected on the RP2040. */
        (void)quiet();
        return false;
    }
    initialized = true;
    if (!quiet())
    {
        return failed();
    }
    return true;
}

bool pn532_frontend_prepare_tag(uint8_t protocol)
{
    ready = false;
    field_enabled = false;
    role = FRONTEND_IDLE;
    reader_protocol = 0u;
    if ((protocol != PN532_PROTOCOL_NFCA && protocol != PN532_PROTOCOL_NFCV) ||
        !pn532_frontend_init() || !quiet())
    {
        return failed();
    }
    /* External envelope RX, direct load modulation, no delayed SIGOUT. */
    /* The maximum switched conductance improves long NFC-V responses on
     * the PM3 bench. Keep the inactive load at zero and NFC-A at its
     * qualified setting; this does not change the subcarrier waveform. */
    uint8_t conductance = protocol == PN532_PROTOCOL_NFCV ? CIU_GS_N_OFF_MOD_MAX
                                                          : CIU_GS_N_OFF_MOD_8;
    /* NFC-A follows the reference card setup: start RxWait with the
     * external reader field, rather than before any field is present. */
    const uint8_t mode =
        CIU_MODE_SIGIN_HIGH | CIU_MODE_DETECT_OFF |
        (protocol == PN532_PROTOCOL_NFCA ? CIU_MODE_RX_WAIT_RF : 0u);
#ifdef PNU_RF_TRACE
    if (protocol == PN532_PROTOCOL_NFCA)
    {
        conductance = nfca_gsn_off;
    }
#endif
    if (!pn532_update_register(CIU_TX_AUTO, CIU_TX_AUTO_CONTROL_MASK, 0u) ||
        !pn532_update_register(CIU_TX_CONTROL, CIU_TX_CONTROL_TAG_MASK, 0u) ||
        !pn532_update_register(CIU_TX_SEL,
                               CIU_TX_DRIVER_MASK | CIU_TX_SIGOUT_MASK,
                               CIU_TX_SIGOUT_ENVELOPE) ||
        !pn532_update_register(
            PN532_NFC_WI_CONTROL, PN532_NFC_WI_COUNTER_MASK, 0u) ||
        !pn532_update_register(CIU_TX_MODE, CIU_TX_MIX_INVERT_MASK, 0u))
    {
        return failed();
    }
    /* Field detection, envelope receive and load modulation. */
    if (!pn532_update_register(CIU_MODE, CIU_MODE_CONTROL_MASK, mode) ||
        !pn532_update_register(
            CIU_MANUAL_RCV, CIU_MANUAL_RCV_DELAY_SIGOUT, 0u) ||
        !pn532_update_register(CIU_SIC_CLK_EN, CIU_SIC_CLK_P34_ENABLE, 0u) ||
        !pn532_write_register(CIU_GS_N_OFF, conductance) ||
        !pn532_update_register(CIU_COMMAND, CIU_COMMAND_POWER_MASK, 0u))
    {
        return failed();
    }
#ifdef PNU_RF_TRACE
    uint8_t observed;
    if (!pn532_read_register(CIU_GS_N_OFF, &observed) ||
        observed != conductance || !pn532_frontend_trace_snapshot())
    {
        return failed();
    }
#endif
    role = FRONTEND_TAG;
    return true;
}

bool pn532_frontend_arm_tag(void)
{
    if (role != FRONTEND_TAG)
    {
        return failed();
    }
    uint8_t switch_control;
    if (!pn532_read_register(PN532_CONTROL_SWITCH_RNG, &switch_control) ||
        (switch_control & PN532_SVDD_OVERLOAD) != 0u ||
        !pn532_update_register(PN532_CONTROL_SWITCH_RNG,
                               PN532_SVDD_CONTROL_MASK,
                               PN532_SVDD_ENABLE) ||
        !pn532_write_register(CIU_COMMAND, CIU_COMMAND_RECEIVE) ||
        !pn532_update_register(
            CIU_TX_SEL, CIU_TX_DRIVER_MASK, CIU_TX_DRIVER_SIGIN))
    {
        return failed();
    }
    ready = true;
    return true;
}

typedef struct
{
    uint8_t demod;
    uint8_t gain;
} reader_rx_settings_t;

static reader_rx_settings_t reader_rx_settings(uint8_t protocol)
{
    static const reader_rx_settings_t profiles[] = {
        [NFCA_RX_Q_33_DB] = {CIU_DEMOD_FIXED_Q, CIU_RF_RX_GAIN_33_DB},
        [NFCA_RX_I_18_DB] = {CIU_DEMOD_FIXED_I, CIU_RF_RX_GAIN_18_DB},
        [NFCA_RX_I_33_DB] = {CIU_DEMOD_FIXED_I, CIU_RF_RX_GAIN_33_DB},
        [NFCA_RX_Q_18_DB] = {CIU_DEMOD_FIXED_Q, CIU_RF_RX_GAIN_18_DB},
    };
    /* NFC-V always uses the qualified Q/33 dB profile. */
    return profiles[protocol == PN532_PROTOCOL_NFCA ? nfca_rx_profile
                                                    : NFCA_RX_Q_33_DB];
}

bool pn532_frontend_prepare_reader(uint8_t protocol)
{
    if (ready && role == FRONTEND_READER && reader_protocol == protocol)
    {
        return true;
    }
    const bool enable = field_enabled;
    ready = false;
    if ((protocol != PN532_PROTOCOL_NFCA && protocol != PN532_PROTOCOL_NFCV) ||
        !pn532_frontend_init() || !quiet() || !save_reader_config())
    {
        return failed();
    }
    /* SIGIN high is the unmodulated carrier. The FeliCa comparator route
     * keeps Receive active between frames. Fix the physical channel because
     * its decoder cannot freeze I/Q selection on these frames. NFC-A uses
     * the fixed profile selected while idle, defaulting to Q/33 dB. NFC-V
     * keeps its verified Q/33 dB path and existing high-pass filter. */
    if (!pn532_update_register(CIU_MODE,
                               CIU_MODE_CONTROL_MASK,
                               CIU_MODE_SIGIN_HIGH | CIU_MODE_DETECT_OFF) ||
        !pn532_write_register(CIU_TX_MODE, CIU_TX_MODE_NFCA_106) ||
        !pn532_write_register(CIU_RX_MODE, CIU_RX_MODE_COMPARATOR) ||
        !pn532_write_register(CIU_RX_SEL, CIU_RX_SEL_ANALOG))
    {
        return failed();
    }
    const reader_rx_settings_t rx = reader_rx_settings(protocol);
    /* Receive threshold, demodulation, filter and external clock. */
    if (!pn532_update_register(
            CIU_RX_THRESHOLD, CIU_RX_THRESHOLD_MASK, CIU_RX_THRESHOLD_READER) ||
        !pn532_update_register(
            CIU_DEMOD, CIU_DEMOD_MASK, CIU_DEMOD_PLL_READER | rx.demod) ||
        !pn532_update_register(CIU_MANUAL_RCV,
                               CIU_MANUAL_RCV_MASK,
                               CIU_MANUAL_HPCF |
                                   (protocol == PN532_PROTOCOL_NFCA
                                        ? CIU_HPCF_848_KHZ
                                        : CIU_HPCF_106_KHZ)) ||
        !pn532_update_register(
            CIU_TX_BIT_PHASE, CIU_RX_CLOCK_EXTERNAL, CIU_RX_CLOCK_EXTERNAL))
    {
        return failed();
    }
    /* Receiver gain and initiator routing. */
    if (!pn532_update_register(CIU_RF_CFG, CIU_RF_RX_GAIN_MASK, rx.gain) ||
        !pn532_update_register(CIU_SIC_CLK_EN, CIU_SIC_CLK_P34_ENABLE, 0u) ||
        !pn532_update_register(
            CIU_CONTROL, CIU_CONTROL_INITIATOR, CIU_CONTROL_INITIATOR))
    {
        return failed();
    }
    /* Carrier and modulation drive. */
    if (!pn532_update_register(
            CIU_GS_N_ON, CIU_CW_GS_N_MASK, CIU_CW_GS_N_MASK) ||
        !pn532_write_register(CIU_CW_GS_P, CIU_CW_GS_P_MAX) ||
        !pn532_write_register(CIU_MOD_GS_P, CIU_MOD_GS_P_READER) ||
        !pn532_update_register(
            CIU_TX_AUTO, CIU_TX_AUTO_CONTROL_MASK, CIU_TX_FORCE_100_ASK))
    {
        return failed();
    }
    gpio_init(TAG_LOAD_GPIO);
    gpio_put(TAG_LOAD_GPIO, true);
    gpio_set_dir(TAG_LOAD_GPIO, GPIO_OUT);
    gpio_set_drive_strength(TAG_LOAD_GPIO, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(TAG_LOAD_GPIO, GPIO_SLEW_RATE_FAST);
    if (!pn532_update_register(PN532_CONTROL_SWITCH_RNG,
                               PN532_SVDD_CONTROL_MASK,
                               PN532_SVDD_ENABLE) ||
        !pn532_write_register(CIU_TX_SEL,
                              CIU_TX_DRIVER_SIGIN | CIU_TX_SIGOUT_COMPARATOR) ||
        !pn532_write_register(CIU_COMMAND, CIU_COMMAND_RECEIVE) ||
        !pn532_update_register(CIU_TX_CONTROL,
                               CIU_TX_CONTROL_READER_MASK,
                               CIU_TX_INVERT_2_ON |
                                   (enable ? CIU_TX_RF_ENABLE_MASK : 0u)))
    {
        return failed();
    }
    reader_protocol = protocol;
    role = FRONTEND_READER;
    ready = true;
    field_enabled = enable;
    if (enable)
    {
        sleep_ms(PN532_FIELD_SETTLE_MS);
    }
    return true;
}

bool pn532_frontend_set_field(bool enabled)
{
    if (!enabled)
    {
        /* The shared SIGIN pin belongs to passive emulation until its owner
         * stops it. Closing a reader host session must not disconnect a tag. */
        if (ready && role == FRONTEND_TAG)
        {
            return true;
        }
        if (ready && role == FRONTEND_READER)
        {
            if (!pn532_update_register(
                    CIU_TX_AUTO, CIU_TX_AUTO_FIELD_MASK, 0u) ||
                !pn532_update_register(
                    CIU_TX_CONTROL, CIU_TX_RF_ENABLE_MASK, 0u) ||
                !pn532_write_register(CIU_COMMAND, CIU_COMMAND_IDLE))
            {
                return failed();
            }
            field_enabled = false;
            return true;
        }
        pn532_frontend_stop();
        return initialized;
    }
    if (role != FRONTEND_READER &&
        !pn532_frontend_prepare_reader(PN532_PROTOCOL_NFCV))
    {
        return false;
    }
    if (!pn532_update_register(
            CIU_TX_AUTO, CIU_TX_AUTO_CONTROL_MASK, CIU_TX_FORCE_100_ASK) ||
        !pn532_update_register(
            CIU_TX_CONTROL, CIU_TX_RF_ENABLE_MASK, CIU_TX_RF_ENABLE_MASK))
    {
        return failed();
    }
    if (!field_enabled)
    {
        sleep_ms(PN532_FIELD_SETTLE_MS);
    }
    field_enabled = true;
    /* Arm the comparator after field settling, for every physical exchange.
     * The internal decoder can leave Receive after an unsupported RF frame. */
    if (!pn532_write_register(CIU_COMMAND, CIU_COMMAND_IDLE) ||
        !pn532_write_register(CIU_COMMAND, CIU_COMMAND_RECEIVE))
    {
        return failed();
    }
    return true;
}

bool nfc_frontend_initialized(void)
{
    return initialized;
}

bool nfc_rf_ready(void)
{
    return ready;
}
