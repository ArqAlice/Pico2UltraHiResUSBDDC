/*
* Copyright (c) 2025 ArqAlice
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _FFT_FIR_CORE0_H_
#define _FFT_FIR_CORE0_H_

#include <stdint.h>
#include "pico/stdlib.h"
#include "fft_fir_coef.h"

extern void fft_fir_core0_init(void);
extern void fft_fir_core0_reset(void);
extern const FFT_FIR_PROFILE *fft_fir_core0_select_profile(uint32_t freq, uint16_t ratio, bool is_high_power);
extern uint32_t __not_in_flash_func(fft_fir_core0_process_block)(
    const FFT_FIR_PROFILE *profile,
    const float *in_L,
    const float *in_R,
    float *out_L,
    float *out_R);
extern uint32_t __not_in_flash_func(fft_fir_core0_process_block_stage)(
    const FFT_FIR_PROFILE *profile,
    const float *in_L,
    const float *in_R,
    float *out_L,
    float *out_R,
    uint8_t stage);

#endif /* _FFT_FIR_CORE0_H_ */
