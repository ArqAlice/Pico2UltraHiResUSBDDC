/*
* Copyright (c) 2025 ArqAlice
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "fft_fir_core0.h"
#include <string.h>
#include "pico/stdlib.h"
#include "arm_math.h"
#include "arm_const_structs.h"

#define FFT_FIR_COMPLEX_LEN (FFT_FIR_N * 2)

static float fft_in[FFT_FIR_COMPLEX_LEN];
static float fft_work[FFT_FIR_COMPLEX_LEN];
static float overlap_L[FFT_FIR_MAX_PHASE_LEN];
static float overlap_R[FFT_FIR_MAX_PHASE_LEN];

static void fft_fir_update_overlap(float *overlap, const float *input, uint32_t input_len, uint32_t phase_len)
{
    uint32_t pre = phase_len - 1;
    if (pre == 0)
        return;

    if (input_len >= pre)
    {
        memcpy(overlap, input + (input_len - pre), pre * sizeof(float));
    }
    else
    {
        uint32_t keep = pre - input_len;
        memmove(overlap, overlap + input_len, keep * sizeof(float));
        memcpy(overlap + keep, input, input_len * sizeof(float));
    }
}

static void __not_in_flash_func(fft_fir_process_channel)(
    const FFT_FIR_PROFILE *profile,
    const float *input,
    float *output,
    float *overlap)
{
    uint32_t pre = profile->phase_len - 1;
    uint32_t input_len = profile->input_len;
    uint32_t up_ratio = profile->up_ratio;
    const float *h_base = profile->h_fft;

    for (uint32_t i = 0; i < pre; i++)
    {
        uint32_t idx = i * 2;
        fft_in[idx] = overlap[i];
        fft_in[idx + 1] = 0.0f;
    }
    for (uint32_t i = 0; i < input_len; i++)
    {
        uint32_t idx = (pre + i) * 2;
        fft_in[idx] = input[i];
        fft_in[idx + 1] = 0.0f;
    }

    arm_cfft_f32(&arm_cfft_sR_f32_len512, fft_in, 0, 1);

    for (uint32_t phase = 0; phase < up_ratio; phase++)
    {
        const float *h = h_base + (phase * FFT_FIR_COMPLEX_LEN);
        memcpy(fft_work, fft_in, sizeof(float) * FFT_FIR_COMPLEX_LEN);

        for (uint32_t k = 0; k < FFT_FIR_N; k++)
        {
            uint32_t idx = k * 2;
            float xr = fft_work[idx];
            float xi = fft_work[idx + 1];
            float hr = h[idx];
            float hi = h[idx + 1];
            fft_work[idx] = (xr * hr) - (xi * hi);
            fft_work[idx + 1] = (xr * hi) + (xi * hr);
        }

        arm_cfft_f32(&arm_cfft_sR_f32_len512, fft_work, 1, 1);

        for (uint32_t i = 0; i < input_len; i++)
        {
            uint32_t idx = (pre + i) * 2;
            output[i * up_ratio + phase] = fft_work[idx];
        }
    }

    fft_fir_update_overlap(overlap, input, input_len, profile->phase_len);
}

void fft_fir_core0_init(void)
{
    fft_fir_core0_reset();
}

void fft_fir_core0_reset(void)
{
    memset(overlap_L, 0, sizeof(overlap_L));
    memset(overlap_R, 0, sizeof(overlap_R));
}

const FFT_FIR_PROFILE *fft_fir_core0_select_profile(uint32_t freq, uint16_t ratio, bool is_high_power)
{
    switch (freq)
    {
    case 44100:
        if (ratio == 8)
            return is_high_power ? &fft_fir_profile_352800_22050_u8_hp : &fft_fir_profile_352800_22050_u8_lp;
        if (ratio == 4)
            return is_high_power ? &fft_fir_profile_176400_22050_u4_hp : &fft_fir_profile_176400_22050_u4_lp;
        break;
    case 88200:
        if (ratio == 4)
            return is_high_power ? &fft_fir_profile_352800_24000_u4_hp : &fft_fir_profile_352800_24000_u4_lp;
        if (ratio == 2)
            return is_high_power ? &fft_fir_profile_176400_24000_u2_hp : &fft_fir_profile_176400_24000_u2_lp;
        break;
    case 176400:
        if (ratio == 2)
            return is_high_power ? &fft_fir_profile_352800_24000_u2_hp : &fft_fir_profile_352800_24000_u2_lp;
        break;
    case 48000:
        if (ratio == 8)
            return is_high_power ? &fft_fir_profile_384000_24000_u8_hp : &fft_fir_profile_384000_24000_u8_lp;
        if (ratio == 4)
            return is_high_power ? &fft_fir_profile_192000_24000_u4_hp : &fft_fir_profile_192000_24000_u4_lp;
        break;
    case 96000:
        if (ratio == 4)
            return is_high_power ? &fft_fir_profile_384000_24000_u4_hp : &fft_fir_profile_384000_24000_u4_lp;
        if (ratio == 2)
            return is_high_power ? &fft_fir_profile_192000_24000_u2_hp : &fft_fir_profile_192000_24000_u2_lp;
        break;
    case 192000:
        if (ratio == 2)
            return is_high_power ? &fft_fir_profile_384000_24000_u2_hp : &fft_fir_profile_384000_24000_u2_lp;
        break;
    default:
        break;
    }
    return NULL;
}

uint32_t __not_in_flash_func(fft_fir_core0_process_block)(
    const FFT_FIR_PROFILE *profile,
    const float *in_L,
    const float *in_R,
    float *out_L,
    float *out_R)
{
    if (profile == NULL)
        return 0;

    fft_fir_process_channel(profile, in_L, out_L, overlap_L);
    fft_fir_process_channel(profile, in_R, out_R, overlap_R);

    return profile->input_len * profile->up_ratio;
}
