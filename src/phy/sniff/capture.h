#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Exclusive RF owner; 2 or 3 synchronized input channels on the 8/fc grid.
 * Caller services within 30 ms. read writes only the selected channels. */
bool sniff_capture_start(const uint8_t *pins, uint8_t channels);
void sniff_capture_stop(void);
void sniff_capture_pause(void);
bool sniff_capture_read(uint32_t levels[3]);
bool sniff_capture_overrun(void);
