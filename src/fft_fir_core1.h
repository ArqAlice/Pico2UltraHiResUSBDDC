/*
* Copyright (c) 2025 ArqAlice
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _FFT_FIR_CORE1_H_
#define _FFT_FIR_CORE1_H_

#include <stdint.h>
#include "pico/stdlib.h"
#include "fft_fir_coef.h"

extern void fft_fir_core1_init(void);
extern void fft_fir_core1_reset(void);
extern uint32_t __not_in_flash_func(fft_fir_core1_process_block)(
    const FFT_FIR_PROFILE *profile,
    const float *in_L,
    const float *in_R,
    float *out_L,
    float *out_R);

#endif /* _FFT_FIR_CORE1_H_ */
