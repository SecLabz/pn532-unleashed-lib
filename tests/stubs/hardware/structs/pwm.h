#pragma once

#include <stdint.h>

#define PWM_CH0_CSR_EN_BITS 1u

typedef struct
{
    struct
    {
        uint32_t csr;
    } slice[8];
} pwm_hw_t;

extern pwm_hw_t *pwm_hw;
