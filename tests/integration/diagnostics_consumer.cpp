#include "pn532_unleashed.h"
#include "pn532_unleashed/diagnostics.h"
#include "pn532_unleashed/endian.h"

static_assert(NFCA_RX_Q_33_DB == 0);
static_assert(NFCA_RX_I_18_DB == 1);
static_assert(NFCA_RX_I_33_DB == 2);
static_assert(NFCA_RX_Q_18_DB == 3);

/* Compile and link the public diagnostics API from C++; never flashed. */
int main()
{
    uint8_t bytes[4] = {};
    write_le16(bytes, 1u);
    write_le32(bytes, read_le16(bytes));
    (void)read_le32(bytes);
    (void)nfc_transport_name();
    (void)nfc_frontend_initialized();
    (void)nfc_rf_ready();
    (void)nfca_reader_rx_profile();
    (void)nfca_reader_set_rx_profile(NFCA_RX_Q_33_DB);
#ifdef PNU_RF_TRACE
    (void)nfc_trace_gsn();
    (void)nfc_trace_fdt_fc();
    (void)nfc_trace_configure(0u, 0u);
    (void)nfc_trace_frontend(nullptr, 0u);
    (void)nfc_trace_nfca(0u, nullptr, 0u);
    (void)nfc_trace_reader(nullptr, 0u);
    nfc_trace_finish(0u);
#endif
#ifdef PNU_HSU_TRACE
    (void)nfc_trace_transport(nullptr, 0u);
#endif
    return 0;
}
