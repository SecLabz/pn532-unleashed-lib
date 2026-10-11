#include "board/resources.h"

#include "board/activity_led.h"
#include "board/board_config.h"

#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/pwm.h"

#include <stddef.h>

bool board_resources_claim(nfc_transport_t transport)
{
    if (transport != NFC_TRANSPORT_SPI && transport != NFC_TRANSPORT_HSU)
    {
        return false;
    }
    if (pio_sm_is_claimed(RF_RX_PIO, RF_RX_SM) ||
        pio_sm_is_claimed(RF_TX_PIO, RF_TX_SM) ||
        pio_sm_is_claimed(PN532_CONTROL_PIO, PN532_CONTROL_SM) ||
        irq_has_shared_handler(RF_RX_IRQ) ||
        irq_get_exclusive_handler(RF_RX_IRQ) != NULL ||
        (pwm_hw->slice[NFCA_TIMER_SLICE].csr & PWM_CH0_CSR_EN_BITS) != 0u ||
        !board_activity_led_available() ||
        (transport == NFC_TRANSPORT_HSU &&
         pio_sm_is_claimed(PN532_HSU_PIO, PN532_HSU_RX_SM)))
    {
        return false;
    }
    pio_sm_claim(RF_RX_PIO, RF_RX_SM);
    pio_sm_claim(RF_TX_PIO, RF_TX_SM);
    pio_sm_claim(PN532_CONTROL_PIO, PN532_CONTROL_SM);
#if PNU_ACTIVITY_LED
    pio_sm_claim(ACTIVITY_LED_PIO, ACTIVITY_LED_SM);
#endif
    if (transport == NFC_TRANSPORT_HSU)
    {
        pio_sm_claim(PN532_HSU_PIO, PN532_HSU_RX_SM);
    }
    return true;
}
