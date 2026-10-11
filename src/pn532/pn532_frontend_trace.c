#include "pn532/pn532_frontend.h"

#ifdef PNU_RF_TRACE
#include "pn532/pn532_command.h"
#include "pn532/pn532_registers.h"

#include <stdio.h>

static const uint16_t trace_registers[] = {
    PN532_CONTROL_SWITCH_RNG,
    PN532_NFC_WI_CONTROL,
    CIU_MODE,
    CIU_TX_MODE,
    CIU_RX_MODE,
    CIU_TX_CONTROL,
    CIU_TX_AUTO,
    CIU_TX_SEL,
    CIU_RX_SEL,
    CIU_RX_THRESHOLD,
    CIU_DEMOD,
    CIU_MANUAL_RCV,
    CIU_GS_N_OFF,
    CIU_TX_BIT_PHASE,
    CIU_RF_CFG,
    CIU_GS_N_ON,
    CIU_CW_GS_P,
    CIU_MOD_GS_P,
    CIU_SIC_CLK_EN,
    CIU_COMMAND,
};
static uint8_t
    trace_values[sizeof(trace_registers) / sizeof(trace_registers[0])];
static bool trace_config_valid;

bool pn532_frontend_trace_snapshot(void)
{
    trace_config_valid = false;
    for (uint16_t i = 0u; i < sizeof(trace_values); i++)
    {
        if (!pn532_read_register(trace_registers[i], &trace_values[i]))
        {
            return false;
        }
    }
    trace_config_valid = true;
    return true;
}

uint16_t nfc_trace_frontend(char *output, uint16_t capacity)
{
    if (!trace_config_valid)
    {
        return 0u;
    }
    uint16_t used = 0u;
    for (uint16_t i = 0u; i < sizeof(trace_values); i++)
    {
        const int count = snprintf(&output[used],
                                   capacity - used,
                                   "%04X:%02X%s",
                                   trace_registers[i],
                                   trace_values[i],
                                   i + 1u == sizeof(trace_values) ? "\n" : ",");
        if (count < 0 || (uint16_t)count >= capacity - used)
        {
            return 0u;
        }
        used += (uint16_t)count;
    }
    return used;
}

#endif
