/* Real lifecycle, engine and card state; only hardware is substituted. */
#include "board/resources.h"
#include "emulation/emulation.h"
#include "phy/nfca/picc/nfca_rx.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "phy/nfcv/picc/nfcv_rx.h"
#include "pn532/pn532_frontend.h"
#include "pn532/pn532_transport.h"
#include "pn532_unleashed.h"

#include "hardware/clocks.h"

static uint32_t clock_hz = PNU_REQUIRED_SYS_CLOCK_HZ;
static uint32_t hardware_calls;
static bool arm_failure;

uint32_t host_snapshot_hardware_calls(void)
{
    return hardware_calls;
}
void host_snapshot_clock(uint32_t hz)
{
    clock_hz = hz;
}
void host_snapshot_arm_failure(bool fail)
{
    arm_failure = fail;
}
uint32_t clock_get_hz(enum clock_index clock)
{
    (void)clock;
    return clock_hz;
}
uint32_t time_us_32(void)
{
    return 200000u;
}
bool board_resources_claim(nfc_transport_t transport)
{
    (void)transport;
    return true;
}
void pn532_transport_select(nfc_transport_t transport)
{
    (void)transport;
}
bool pn532_frontend_init(void)
{
    return true;
}
bool pn532_frontend_stop(void)
{
    hardware_calls++;
    return true;
}
bool pn532_frontend_prepare_tag(uint8_t protocol)
{
    hardware_calls++;
    return protocol == PN532_PROTOCOL_NFCA || protocol == PN532_PROTOCOL_NFCV;
}
bool pn532_frontend_arm_tag(void)
{
    hardware_calls++;
    return !arm_failure;
}
void tag_board_init(void)
{
    hardware_calls++;
}
bool tag_phy_init(void)
{
    hardware_calls++;
    return true;
}
void tag_phy_stop(void)
{
    hardware_calls++;
}
bool nfca_rx_init(nfca_rx_frame_handler_t handler)
{
    hardware_calls++;
    return handler != NULL;
}
void nfca_rx_deinit(void)
{
    hardware_calls++;
}
bool nfcv_rx_init(nfcv_rx_frame_handler_t handler)
{
    hardware_calls++;
    return handler != NULL;
}
void nfcv_rx_deinit(void)
{
    hardware_calls++;
}
bool nfcv_tx_init(void)
{
    hardware_calls++;
    return true;
}
bool nfcv_tx_send_response(uint32_t eof_us,
                           const uint8_t *data,
                           uint16_t size_bytes)
{
    (void)eof_us;
    hardware_calls++;
    return data != NULL && size_bytes != 0u;
}
void nfca_tx_send_response(nfca_tag_response_t *response)
{
    (void)response;
    hardware_calls++;
}
bool nfca_reader_stop(void)
{
    return true;
}
bool nfcv_reader_stop(void)
{
    return true;
}
void nfca_access_reset(void)
{
}
void mifare_recovery_reset(void)
{
}

static void custom_nfca(void *context,
                        const nfca_tag_request_t *request,
                        nfca_response_t *response)
{
    (void)context;
    (void)request;
    nfca_response_silent(response);
}
static void custom_nfcv(void *context,
                        const nfcv_frame_t *request,
                        nfcv_tag_response_t *response)
{
    (void)context;
    (void)request;
    nfcv_reply_silent(response);
}
int host_snapshot_custom(bool nfcv)
{
    return nfcv ? nfcv_emulation_start(custom_nfcv, NULL)
                : nfca_emulation_start(custom_nfca, NULL);
}
