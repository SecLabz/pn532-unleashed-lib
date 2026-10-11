#include "pn532_unleashed/integrity.h"

uint16_t host_protocol_crc(bool nfca, const uint8_t *bytes, size_t size_bytes)
{
    return nfca ? nfca_crc16(bytes, size_bytes) : nfcv_crc16(bytes, size_bytes);
}
