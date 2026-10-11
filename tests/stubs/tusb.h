#pragma once
#include <stdbool.h>
#include <stdint.h>
bool tud_mounted(void);
bool tud_cdc_connected(void);
uint32_t tud_cdc_write_available(void);
uint32_t tud_cdc_write(const void *bytes, uint32_t count);
uint32_t tud_cdc_write_flush(void);
uint32_t tud_cdc_available(void);
uint32_t tud_cdc_read(void *bytes, uint32_t count);
void tud_cdc_read_flush(void);
bool tud_cdc_write_clear(void);
