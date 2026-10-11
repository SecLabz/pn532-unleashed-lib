#pragma once

#include "pn532/pn532_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PN532_CMD_GET_FIRMWARE_VERSION 0x02u
#define PN532_CMD_READ_REGISTER 0x06u
#define PN532_CMD_WRITE_REGISTER 0x08u
#define PN532_CMD_SAM_CONFIGURATION 0x14u
#define PN532_IC_ID 0x32u
#define PN532_SAM_NORMAL_MODE 0x01u
#define PN532_COMMAND_MAX_PAYLOAD 32u

/* PN532 command framing shared by SPI and HSU. No host command may run inside
 * an RF IRQ or a timed exchange. Returns payload size or -1 on failure. */
int pn532_command(uint8_t command,
                  const uint8_t *payload,
                  size_t payload_size,
                  uint8_t *response,
                  size_t capacity);
bool pn532_read_register(uint16_t address, uint8_t *value);
bool pn532_write_register(uint16_t address, uint8_t value);
bool pn532_update_register(uint16_t address, uint8_t mask, uint8_t value);
