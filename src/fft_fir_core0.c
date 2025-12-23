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

#define FFT_FIR_PACKED_LEN (FFT_FIR_MAX_PACKED_LEN)
#define FFT_FIR_TIME_LEN (FFT_FIR_MAX_FFT_LEN)

static float fft_time[FFT_FIR_TIME_LEN];
static float fft_freq[FFT_FIR_PACKED_LEN];
typedef struct
{
    uint32_t head_hist_index;
    uint32_t tail_hist_index;
    uint32_t tail_out_index;
    uint32_t tail_in_fill;
    float head_x_hist[FFT_FIR_MAX_HEAD_PARTS][FFT_FIR_HEAD_FFT_LEN];
    float tail_x_hist[FFT_FIR_MAX_TAIL_PARTS][FFT_FIR_TAIL_FFT_LEN];
    float head_overlap[FFT_FIR_MAX_UP_RATIO][FFT_FIR_HEAD_BLOCK_LEN];
    float tail_overlap[FFT_FIR_MAX_UP_RATIO][FFT_FIR_TAIL_BLOCK_LEN];
    float tail_out[FFT_FIR_MAX_UP_RATIO][FFT_FIR_TAIL_BLOCK_LEN];
    float tail_in[FFT_FIR_TAIL_BLOCK_LEN];
} FFT_FIR_STATE;

static FFT_FIR_STATE state_L;
static FFT_FIR_STATE state_R;

static arm_rfft_fast_instance_f32 head_rfft;
static arm_rfft_fast_instance_f32 tail_rfft;
static bool rfft_ready = false;

static void fft_fir_init_rfft(void)
{
    if (rfft_ready)
        return;
    arm_rfft_fast_init_f32(&head_rfft, FFT_FIR_HEAD_FFT_LEN);
    arm_rfft_fast_init_f32(&tail_rfft, FFT_FIR_TAIL_FFT_LEN);
    rfft_ready = true;
}

static void __not_in_flash_func(rfft_packed_mul_store)(
    float *__restrict dst,
    const float *__restrict x,
    const float *__restrict h,
    uint32_t fft_len)
{
    dst[0] = x[0] * h[0];
    dst[1] = x[1] * h[1];
    uint32_t half = fft_len / 2;
    if (half > 1)
        arm_cmplx_mult_cmplx_f32(x + 2, h + 2, dst + 2, half - 1);
}

static void __not_in_flash_func(rfft_packed_mul_accum)(
    float *__restrict dst,
    const float *__restrict x,
    const float *__restrict h,
    float *__restrict scratch,
    uint32_t fft_len)
{
    dst[0] += x[0] * h[0];
    dst[1] += x[1] * h[1];
    uint32_t half = fft_len / 2;
    if (half > 1)
    {
        arm_cmplx_mult_cmplx_f32(x + 2, h + 2, scratch + 2, half - 1);
        arm_add_f32(dst + 2, scratch + 2, dst + 2, (half - 1) * 2);
    }
}

static void __not_in_flash_func(fft_fir_process_channel)(
    const FFT_FIR_PROFILE *profile,
    const float *input,
    float *output,
    FFT_FIR_STATE *state)
{
    uint32_t input_len = profile->input_len;
    uint32_t up_ratio = profile->up_ratio;
    uint32_t head_fft_len = profile->head_fft_len;
    uint32_t head_block_len = profile->head_block_len;
    uint32_t head_parts = profile->head_parts;
    uint32_t tail_fft_len = profile->tail_fft_len;
    uint32_t tail_block_len = profile->tail_block_len;
    uint32_t tail_parts = profile->tail_parts;
    const float *h_head = profile->h_head_fft;
    const float *h_tail = profile->h_tail_fft;

    fft_fir_init_rfft();

    if (input_len == 0 || input_len != head_block_len)
        return;
    if (head_fft_len != (head_block_len * 2))
        return;
    if (tail_block_len < head_block_len || (tail_block_len % head_block_len) != 0)
        return;
    if (tail_parts > 0 && tail_fft_len != (tail_block_len * 2))
        return;
    if (head_parts == 0 || head_parts > FFT_FIR_MAX_HEAD_PARTS)
        return;
    if (tail_parts > FFT_FIR_MAX_TAIL_PARTS)
        return;
    if (up_ratio == 0 || up_ratio > FFT_FIR_MAX_UP_RATIO)
        return;

    if (head_fft_len > FFT_FIR_MAX_FFT_LEN || tail_fft_len > FFT_FIR_MAX_FFT_LEN)
        return;

    memcpy(fft_time, input, sizeof(float) * input_len);
    memset(fft_time + input_len, 0, sizeof(float) * (head_fft_len - input_len));

    arm_rfft_fast_f32(&head_rfft, fft_time, fft_freq, 0);
    memcpy(state->head_x_hist[state->head_hist_index], fft_freq, sizeof(float) * head_fft_len);

    for (uint32_t phase = 0; phase < up_ratio; phase++)
    {
        const float *h_phase = h_head + (phase * head_parts * head_fft_len);
        if (head_parts == 1)
        {
            rfft_packed_mul_store(fft_freq, state->head_x_hist[state->head_hist_index], h_phase, head_fft_len);
        }
        else if (head_parts == 2)
        {
            const float *x0 = state->head_x_hist[state->head_hist_index];
            const float *x1 = state->head_x_hist[state->head_hist_index ^ 1u];
            rfft_packed_mul_store(fft_freq, x0, h_phase, head_fft_len);
            rfft_packed_mul_accum(fft_freq, x1, h_phase + head_fft_len, fft_time, head_fft_len);
        }
        else
        {
            uint32_t hist_index = state->head_hist_index;
            rfft_packed_mul_store(fft_freq, state->head_x_hist[hist_index], h_phase, head_fft_len);
            if (hist_index == 0)
                hist_index = head_parts - 1;
            else
                hist_index--;
            for (uint32_t part = 1; part < head_parts; part++)
            {
                const float *h = h_phase + (part * head_fft_len);
                rfft_packed_mul_accum(fft_freq, state->head_x_hist[hist_index], h, fft_time, head_fft_len);
                if (hist_index == 0)
                    hist_index = head_parts - 1;
                else
                    hist_index--;
            }
        }

        arm_rfft_fast_f32(&head_rfft, fft_freq, fft_time, 1);

        float *overlap = state->head_overlap[phase];
        float *out = output + phase;
        for (uint32_t i = 0; i < input_len; i++, out += up_ratio)
            *out = fft_time[i] + overlap[i];
        for (uint32_t i = 0; i < input_len; i++)
            overlap[i] = fft_time[i + input_len];
    }

    state->head_hist_index++;
    if (state->head_hist_index >= head_parts)
        state->head_hist_index = 0;

    if (tail_parts > 0)
    {
        memcpy(state->tail_in + state->tail_in_fill, input, sizeof(float) * input_len);
        state->tail_in_fill += input_len;

        if (state->tail_in_fill >= tail_block_len)
        {
            memcpy(fft_time, state->tail_in, sizeof(float) * tail_block_len);
            memset(fft_time + tail_block_len, 0, sizeof(float) * (tail_fft_len - tail_block_len));

            arm_rfft_fast_f32(&tail_rfft, fft_time, fft_freq, 0);
            memcpy(state->tail_x_hist[state->tail_hist_index], fft_freq, sizeof(float) * tail_fft_len);

            for (uint32_t phase = 0; phase < up_ratio; phase++)
            {
                const float *h_phase = h_tail + (phase * tail_parts * tail_fft_len);
                if (tail_parts == 1)
                {
                    const float *x0 = state->tail_x_hist[state->tail_hist_index];
                    rfft_packed_mul_store(fft_freq, x0, h_phase, tail_fft_len);
                }
                else if (tail_parts == 2)
                {
                    const float *x0 = state->tail_x_hist[state->tail_hist_index];
                    const float *x1 = state->tail_x_hist[state->tail_hist_index ^ 1u];
                    rfft_packed_mul_store(fft_freq, x0, h_phase, tail_fft_len);
                    rfft_packed_mul_accum(fft_freq, x1, h_phase + tail_fft_len, fft_time, tail_fft_len);
                }
                else
                {
                    uint32_t hist_index = state->tail_hist_index;
                    rfft_packed_mul_store(fft_freq, state->tail_x_hist[hist_index], h_phase, tail_fft_len);
                    if (hist_index == 0)
                        hist_index = tail_parts - 1;
                    else
                        hist_index--;
                    for (uint32_t part = 1; part < tail_parts; part++)
                    {
                        const float *h = h_phase + (part * tail_fft_len);
                        rfft_packed_mul_accum(fft_freq, state->tail_x_hist[hist_index], h, fft_time, tail_fft_len);
                        if (hist_index == 0)
                            hist_index = tail_parts - 1;
                        else
                            hist_index--;
                    }
                }

                arm_rfft_fast_f32(&tail_rfft, fft_freq, fft_time, 1);

                float *overlap = state->tail_overlap[phase];
                float *tail_out = state->tail_out[phase];
                arm_add_f32(fft_time, overlap, tail_out, tail_block_len);
                for (uint32_t i = 0; i < tail_block_len; i++)
                    overlap[i] = fft_time[i + tail_block_len];
            }

            state->tail_hist_index++;
            if (state->tail_hist_index >= tail_parts)
                state->tail_hist_index = 0;
            state->tail_in_fill = 0;
            state->tail_out_index = 0;
        }

        uint32_t tail_offset = state->tail_out_index;
        if (tail_offset + input_len > tail_block_len)
        {
            tail_offset = 0;
            state->tail_out_index = 0;
        }
        for (uint32_t phase = 0; phase < up_ratio; phase++)
        {
            const float *tail_out = state->tail_out[phase];
            const float *tail_ptr = tail_out + tail_offset;
            float *out = output + phase;
            for (uint32_t i = 0; i < input_len; i++, out += up_ratio)
                *out += tail_ptr[i];
        }
        state->tail_out_index += input_len;
        if (state->tail_out_index >= tail_block_len)
            state->tail_out_index = 0;
    }
}

void fft_fir_core0_init(void)
{
    fft_fir_init_rfft();
    fft_fir_core0_reset();
}

void fft_fir_core0_reset(void)
{
    memset(&state_L, 0, sizeof(state_L));
    memset(&state_R, 0, sizeof(state_R));
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

    fft_fir_process_channel(profile, in_L, out_L, &state_L);
    fft_fir_process_channel(profile, in_R, out_R, &state_R);

    return profile->input_len * profile->up_ratio;
}
