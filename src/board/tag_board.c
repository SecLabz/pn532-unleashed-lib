#include "board/tag_board.h"

#include "hardware/gpio.h"

void tag_board_init(void)
{
    gpio_init(TAG_RX_GPIO);
    gpio_set_dir(TAG_RX_GPIO, GPIO_IN);
    gpio_disable_pulls(TAG_RX_GPIO);
}
