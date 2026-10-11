#include "phy/rf_capture_scratch.h"

#include "pico/stdlib.h"

#include <stddef.h>

static rf_capture_scratch_t scratch;
static rf_capture_scratch_owner_t active_owner;

rf_capture_scratch_t *__not_in_flash_func(rf_capture_scratch_acquire)(
    rf_capture_scratch_owner_t owner)
{
    if (owner == RF_CAPTURE_SCRATCH_NONE ||
        active_owner != RF_CAPTURE_SCRATCH_NONE)
    {
        return NULL;
    }
    active_owner = owner;
    return &scratch;
}

void __not_in_flash_func(rf_capture_scratch_release)(
    rf_capture_scratch_owner_t owner)
{
    if (active_owner == owner)
    {
        active_owner = RF_CAPTURE_SCRATCH_NONE;
    }
}
