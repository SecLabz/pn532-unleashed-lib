#pragma once

#include "board/board_config.h"
#include "phy/timing.h"

#include "hardware/irq.h"

#define TAG_RX_PIO NFCA_RX_PIO
#define TAG_RX_SM NFCA_RX_SM
#define TAG_RX_IRQ NFCA_RX_IRQ

void tag_board_init(void);
