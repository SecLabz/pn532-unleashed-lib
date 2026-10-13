#pragma once

/* Only the registers/fields used by the direct RF frontend. Names follow NXP
 * PN532/C1 Rev. 3.6, sections 8.4.2 and 8.6. Values retain bench settings.
 * https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf */
#define PN532_CONTROL_SWITCH_RNG 0x6106u
#define PN532_NFC_WI_CONTROL 0x610eu
#define CIU_MODE 0x6301u
#define CIU_TX_MODE 0x6302u
#define CIU_RX_MODE 0x6303u
#define CIU_TX_CONTROL 0x6304u
#define CIU_TX_AUTO 0x6305u
#define CIU_TX_SEL 0x6306u
#define CIU_RX_SEL 0x6307u
#define CIU_RX_THRESHOLD 0x6308u
#define CIU_DEMOD 0x6309u
#define CIU_MANUAL_RCV 0x630du
#define CIU_GS_N_OFF 0x6313u
#define CIU_TX_BIT_PHASE 0x6315u
#define CIU_RF_CFG 0x6316u
#define CIU_GS_N_ON 0x6317u
#define CIU_CW_GS_P 0x6318u
#define CIU_MOD_GS_P 0x6319u
#define CIU_SIC_CLK_EN 0x6330u
#define CIU_COMMAND 0x6331u
#define CIU_CONTROL 0x633cu

#define PN532_SVDD_HIDE_SIGIN 0x40u
#define PN532_SVDD_ENABLE 0x10u
#define PN532_SVDD_OVERLOAD 0x20u
#define PN532_SVDD_CONTROL_MASK (PN532_SVDD_HIDE_SIGIN | PN532_SVDD_ENABLE)
#define PN532_NFC_WI_COUNTER_MASK 0x03u

#define CIU_MODE_CONTROL_MASK 0x3cu
#define CIU_MODE_SIGIN_HIGH 0x08u
#define CIU_MODE_DETECT_OFF 0x04u
#define CIU_MODE_RX_WAIT_RF 0x10u
#define CIU_TX_MIX_INVERT_MASK 0x0cu
#define CIU_TX_MODE_NFCA_106 0x00u
#define CIU_RX_MODE_COMPARATOR 0x96u /* CRC, 212 kbit/s, multiple, FeliCa. */

#define CIU_TX_RF_ENABLE_MASK 0x03u
#define CIU_TX_INVERT_2_ON 0x80u
/* RF off polarity, TX2 CW and RF enable. */
#define CIU_TX_CONTROL_TAG_MASK 0x3bu
#define CIU_TX_CONTROL_READER_MASK 0xfbu
#define CIU_TX_AUTO_FIELD_MASK 0xabu /* Automatic field/wakeup/avoidance. */
#define CIU_TX_FORCE_100_ASK 0x40u
#define CIU_TX_AUTO_CONTROL_MASK (CIU_TX_AUTO_FIELD_MASK | CIU_TX_FORCE_100_ASK)

#define CIU_TX_DRIVER_MASK 0x30u
#define CIU_TX_SIGOUT_MASK 0x0fu
#define CIU_TX_DRIVER_SIGIN 0x20u
#define CIU_TX_SIGOUT_ENVELOPE 0x0fu
#define CIU_TX_SIGOUT_COMPARATOR 0x0au
#define CIU_RX_SEL_ANALOG 0x80u
#define CIU_RX_THRESHOLD_MASK 0xf7u
/* Minimum level 8, collision level 4. */
#define CIU_RX_THRESHOLD_READER 0x84u
#define CIU_DEMOD_MASK 0xefu
#define CIU_DEMOD_PLL_READER 0x0du /* TauRcv 3, TauSync 1. */
#define CIU_DEMOD_FIXED_I 0x20u
#define CIU_DEMOD_FIXED_Q 0x60u
#define CIU_MANUAL_RCV_MASK 0x7fu
#define CIU_MANUAL_RCV_DELAY_SIGOUT 0x20u
#define CIU_MANUAL_HPCF 0x04u
#define CIU_HPCF_106_KHZ 0x00u
#define CIU_HPCF_848_KHZ 0x03u

#define CIU_RX_CLOCK_EXTERNAL 0x80u
#define CIU_RF_RX_GAIN_MASK 0x70u
#define CIU_RF_RX_GAIN_18_DB 0x00u
#define CIU_RF_RX_GAIN_33_DB 0x40u
#define CIU_SIC_CLK_P34_ENABLE 0x80u
#define CIU_CONTROL_INITIATOR 0x10u
#define CIU_CW_GS_N_MASK 0xf0u
#define CIU_CW_GS_P_MAX 0x3fu
#define CIU_MOD_GS_P_READER 0x12u
#define CIU_GS_N_OFF_MOD_8 0x08u
#define CIU_GS_N_OFF_MOD_MAX 0x0fu
#define CIU_GS_N_OFF_CW_8 0x80u
#define CIU_GS_N_OFF_CW_MAX 0xf0u

#define CIU_COMMAND_IDLE 0x00u
#define CIU_COMMAND_RECEIVE 0x08u
#define CIU_COMMAND_POWER_MASK 0x30u /* RcvOff and Power-down. */

#define CIU_TEST_SEL1 0x6321u
#define CIU_TEST_SEL2 0x6322u
#define CIU_ANALOG_TEST 0x6328u
#define CIU_RX_MODE_NFCA_MULTIPLE 0x84u
#define CIU_DEMOD_FREEZE_CHANNEL 0x40u
