/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <arm_math.h>
#include "upsampling.h"
#include "debug_with_gpio.h"
#include "ringbuffer.h"
#include "common.h"

// 双二次フィルタ構造体
static arm_biquad_casd_df1_inst_f32 biquad_filter0L;
static arm_biquad_casd_df1_inst_f32 biquad_filter0R;
static arm_biquad_casd_df1_inst_f32 biquad_filter1L;
static arm_biquad_casd_df1_inst_f32 biquad_filter1R;
static arm_biquad_casd_df1_inst_f32 biquad_filter2L;
static arm_biquad_casd_df1_inst_f32 biquad_filter2R;
static arm_biquad_casd_df1_inst_f32 biquad_filter3L;
static arm_biquad_casd_df1_inst_f32 biquad_filter3R;
static arm_biquad_casd_df1_inst_f32 biquad_filter4L;
static arm_biquad_casd_df1_inst_f32 biquad_filter4R;

// 状態バッファ
static float biquad0L_state[SIZE_BQ_FILTER_0 * 4];
static float biquad0R_state[SIZE_BQ_FILTER_0 * 4];
static float biquad1L_state[SIZE_BQ_FILTER_1 * 4];
static float biquad1R_state[SIZE_BQ_FILTER_1 * 4];
static float biquad2L_state[SIZE_BQ_FILTER_2 * 4];
static float biquad2R_state[SIZE_BQ_FILTER_2 * 4];
static float biquad3L_state[SIZE_BQ_FILTER_3 * 4];
static float biquad3R_state[SIZE_BQ_FILTER_3 * 4];
static float biquad4L_state[SIZE_BQ_FILTER_4 * 4];
static float biquad4R_state[SIZE_BQ_FILTER_4 * 4];

// 双二次フィルタ係数
static float biquad0_coeffs[SIZE_BQ_FILTER_0 * 5];
static float biquad1_coeffs[SIZE_BQ_FILTER_1 * 5];
static float biquad2_coeffs[SIZE_BQ_FILTER_2 * 5];
static float biquad3_coeffs[SIZE_BQ_FILTER_3 * 5];
static float biquad4_coeffs[SIZE_BQ_FILTER_4 * 5];

// BiQuad-IIRフィルタの係数を初期化する
static void initialize_bq_filter_coef(void)
{
    for (uint16_t i = 0; i < SIZE_BQ_FILTER_0; i++)
    {
        biquad0_coeffs[i * 5 + 0] = coef_bq_filter_2x_0[i][0];
        biquad0_coeffs[i * 5 + 1] = coef_bq_filter_2x_0[i][1];
        biquad0_coeffs[i * 5 + 2] = coef_bq_filter_2x_0[i][2];
        biquad0_coeffs[i * 5 + 3] = -coef_bq_filter_2x_0[i][4];
        biquad0_coeffs[i * 5 + 4] = -coef_bq_filter_2x_0[i][5];
    }
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter0L,
        SIZE_BQ_FILTER_0,
        biquad0_coeffs,
        biquad0L_state
    );
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter0R,
        SIZE_BQ_FILTER_0,
        biquad0_coeffs,
        biquad0R_state
    );

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_1; i++)
    {
        biquad1_coeffs[i * 5 + 0] = coef_bq_filter_2x_1[i][0];
        biquad1_coeffs[i * 5 + 1] = coef_bq_filter_2x_1[i][1];
        biquad1_coeffs[i * 5 + 2] = coef_bq_filter_2x_1[i][2];
        biquad1_coeffs[i * 5 + 3] = -coef_bq_filter_2x_1[i][4];
        biquad1_coeffs[i * 5 + 4] = -coef_bq_filter_2x_1[i][5];
    }
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter1L,
        SIZE_BQ_FILTER_1,
        biquad1_coeffs,
        biquad1L_state
    );
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter1R,
        SIZE_BQ_FILTER_1,
        biquad1_coeffs,
        biquad1R_state
    );

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_2; i++)
    {
        biquad2_coeffs[i * 5 + 0] = coef_bq_filter_2x_2[i][0];
        biquad2_coeffs[i * 5 + 1] = coef_bq_filter_2x_2[i][1];
        biquad2_coeffs[i * 5 + 2] = coef_bq_filter_2x_2[i][2];
        biquad2_coeffs[i * 5 + 3] = -coef_bq_filter_2x_2[i][4];
        biquad2_coeffs[i * 5 + 4] = -coef_bq_filter_2x_2[i][5];
    }
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter2L,
        SIZE_BQ_FILTER_2,
        biquad2_coeffs,
        biquad2L_state
    );
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter2R,
        SIZE_BQ_FILTER_2,
        biquad2_coeffs,
        biquad2R_state
    );

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_3; i++)
    {
        biquad3_coeffs[i * 5 + 0] = coef_bq_filter_2x_3[i][0];
        biquad3_coeffs[i * 5 + 1] = coef_bq_filter_2x_3[i][1];
        biquad3_coeffs[i * 5 + 2] = coef_bq_filter_2x_3[i][2];
        biquad3_coeffs[i * 5 + 3] = -coef_bq_filter_2x_3[i][4];
        biquad3_coeffs[i * 5 + 4] = -coef_bq_filter_2x_3[i][5];
    }
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter3L,
        SIZE_BQ_FILTER_3,
        biquad3_coeffs,
        biquad3L_state
    );
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter3R,
        SIZE_BQ_FILTER_3,
        biquad3_coeffs,
        biquad3R_state
    );

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_4; i++)
    {
        biquad4_coeffs[i * 5 + 0] = coef_bq_filter_4x_0[i][0];
        biquad4_coeffs[i * 5 + 1] = coef_bq_filter_4x_0[i][1];
        biquad4_coeffs[i * 5 + 2] = coef_bq_filter_4x_0[i][2];
        biquad4_coeffs[i * 5 + 3] = -coef_bq_filter_4x_0[i][4];
        biquad4_coeffs[i * 5 + 4] = -coef_bq_filter_4x_0[i][5];
    }
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter4L,
        SIZE_BQ_FILTER_4,
        biquad4_coeffs,
        biquad4L_state
    );
    arm_biquad_cascade_df1_init_f32(
        &biquad_filter4R,
        SIZE_BQ_FILTER_4,
        biquad4_coeffs,
        biquad4R_state
    );
}

// アップサンプリングフィルタの初期化処理
extern void init_upsampling_filter(void)
{
    initialize_bq_filter_coef();
    clear_bq_filter_delay();
}

// BiQuad-IIRフィルタの遅延バッファをクリアする
extern void clear_bq_filter_delay(void)
{
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_0 * 4; i++)
    {
        biquad0L_state[i] = 0;
        biquad0R_state[i] = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_1 * 4; i++)
    {
        biquad1L_state[i] = 0;
        biquad1R_state[i] = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_2 * 4; i++)
    {
        biquad2L_state[i] = 0;
        biquad2R_state[i] = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_3 * 4; i++)
    {
        biquad3L_state[i] = 0;
        biquad3R_state[i] = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_4 * 4; i++)
    {
        biquad4L_state[i] = 0;
        biquad4R_state[i] = 0;
    }
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_0)(uint32_t length, float *p_in, float *p_out, arm_biquad_casd_df1_inst_f32 *S)
{
    uint32_t length_buffer = length;
    float *NOS_buffer = (float *)malloc(sizeof(float) * (length << 1));
    float *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // BiQuad-IIRフィルタを実行する
    arm_biquad_cascade_df1_f32(S, NOS_buffer, p_out, length << 1);

    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_1)(uint32_t length, float *p_in, float *p_out, arm_biquad_casd_df1_inst_f32 *S)
{
    uint32_t length_buffer = length;
    float *NOS_buffer = (float *)malloc(sizeof(float) * (length << 1));
    float *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // BiQuad-IIRフィルタを実行する
    arm_biquad_cascade_df1_f32(S, NOS_buffer, p_out, length << 1);

    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_2)(uint32_t length, float *p_in, float *p_out, arm_biquad_casd_df1_inst_f32 *S)
{
    uint32_t length_buffer = length;
    float *NOS_buffer = (float *)malloc(sizeof(float) * (length << 1));
    float *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // BiQuad-IIRフィルタを実行する
    arm_biquad_cascade_df1_f32(S, NOS_buffer, p_out, length << 1);

    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_3)(uint32_t length, float *p_in, float *p_out, arm_biquad_casd_df1_inst_f32 *S)
{
    uint32_t length_buffer = length;
    float *NOS_buffer = (float *)malloc(sizeof(float) * (length << 1));
    float *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // BiQuad-IIRフィルタを実行する
    arm_biquad_cascade_df1_f32(S, NOS_buffer, p_out, length << 1);

    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_4x_0)(uint32_t length, float *p_in, float *p_out, arm_biquad_casd_df1_inst_f32 *S)
{
    uint32_t length_buffer = length;
    float *NOS_buffer = (float *)malloc(sizeof(float) * (length << 2));
    float *p_NOS_buffer = NOS_buffer;

    // サンプル数を4倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // BiQuad-IIRフィルタを実行する
    arm_biquad_cascade_df1_f32(S, NOS_buffer, p_out, length << 2);

    free(NOS_buffer);
    return length << 2;
}

// アップサンプリングに使用するメモリを静的確保
static int32_t buffer_copy_from_ep_left_ch[SIZE_EP_BUFFER];
static int32_t buffer_copy_from_ep_right_ch[SIZE_EP_BUFFER];
static float buffer_from_ep_Lch_float[SIZE_EP_BUFFER];
static float buffer_from_ep_Rch_float[SIZE_EP_BUFFER];
static float upsample_buffer_0_L[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static float upsample_buffer_1_L[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static float upsample_buffer_0_R[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static float upsample_buffer_1_R[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];

void __not_in_flash_func(upsampling_process_core0)(void)
{
    uint32_t save;
    // epバッファサイズを取得
    int32_t size_buf = get_size_using(&buffer_ep_Lch);

    // フィルタ演算を実行
    int32_t ref_size, length, deviation, adj;
    int32_t len_L, len_R;

    // アップサンプリングバッファを一定水位に保つようにFBをかける
    deviation = SIZE_BUFFER_FB_THRESHOLD - get_size_using(&buffer_upsr_data_Lch_0);
    if (deviation > 0)
        adj = OSR_ADJ_SIZE;
    else if (deviation < 0)
        adj = -OSR_ADJ_SIZE;
    else
        adj = 0;

    switch (audio_state.freq)
    {
    case 192000:
    case 176400:
        ref_size = (int64_t)audio_state.freq * TIMER0_US / 1000000;
        length = ref_size + adj;

        length = saturation_i32(length, size_buf, 0);
        if (length > 0)
        {
            save = save_and_disable_interrupts();
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_left_ch, length, &buffer_ep_Lch);
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_right_ch, length, &buffer_ep_Rch);
            restore_interrupts(save);

            int32_to_float_array(buffer_copy_from_ep_left_ch, buffer_from_ep_Lch_float, length);
            int32_to_float_array(buffer_copy_from_ep_right_ch, buffer_from_ep_Rch_float, length);

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_2(length, buffer_from_ep_Lch_float, upsample_buffer_0_L, &biquad_filter2L);
                len_R = fast_BQ_filter_2x_2(length, buffer_from_ep_Rch_float, upsample_buffer_0_R, &biquad_filter2R);
            }
            else
            {
                len_L = length;
                len_R = length;
                memcpy(buffer_from_ep_Lch_float, upsample_buffer_0_L, sizeof(float) * length);
                memcpy(buffer_from_ep_Rch_float, upsample_buffer_0_R, sizeof(float) * length);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_0_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_0_R, len_R, &buffer_upsr_data_Rch_0);
            restore_interrupts(save);
        }
        break;

    case 96000:
    case 88200:
        ref_size = (int64_t)audio_state.freq * TIMER0_US / 1000000;
        length = ref_size + adj;

        length = saturation_i32(length, size_buf, 0);
        if (length > 0)
        {
            save = save_and_disable_interrupts();
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_left_ch, length, &buffer_ep_Lch);
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_right_ch, length, &buffer_ep_Rch);
            restore_interrupts(save);

            int32_to_float_array(buffer_copy_from_ep_left_ch, buffer_from_ep_Lch_float, length);
            int32_to_float_array(buffer_copy_from_ep_right_ch, buffer_from_ep_Rch_float, length);

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_1(length, buffer_from_ep_Lch_float, upsample_buffer_0_L, &biquad_filter1L);
                len_L = fast_BQ_filter_2x_2(len_L, upsample_buffer_0_L, upsample_buffer_1_L, &biquad_filter2L);

                len_R = fast_BQ_filter_2x_1(length, buffer_from_ep_Rch_float, upsample_buffer_0_R, &biquad_filter1R);
                len_R = fast_BQ_filter_2x_2(len_R, upsample_buffer_0_R, upsample_buffer_1_R, &biquad_filter2R);
            }
            else
            {
                len_L = fast_BQ_filter_2x_1(length, buffer_from_ep_Lch_float, upsample_buffer_1_L, &biquad_filter1L);
                len_R = fast_BQ_filter_2x_1(length, buffer_from_ep_Rch_float, upsample_buffer_1_R, &biquad_filter1R);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_1_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_1_R, len_R, &buffer_upsr_data_Rch_0);
            restore_interrupts(save);
        }
        break;
    case 48000:
    case 44100:
    default:
        ref_size = (int64_t)audio_state.freq * TIMER0_US / 1000000;
        length = ref_size + adj;

        length = saturation_i32(length, size_buf, 0);
        if (length > 0)
        {
            save = save_and_disable_interrupts();
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_left_ch, length, &buffer_ep_Lch);
            ringbuf_read_array_no_spinlock(buffer_copy_from_ep_right_ch, length, &buffer_ep_Rch);
            restore_interrupts(save);

            int32_to_float_array(buffer_copy_from_ep_left_ch, buffer_from_ep_Lch_float, length);
            int32_to_float_array(buffer_copy_from_ep_right_ch, buffer_from_ep_Rch_float, length);

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_0(length, buffer_from_ep_Lch_float, upsample_buffer_0_L, &biquad_filter0L);
                len_L = fast_BQ_filter_2x_1(len_L, upsample_buffer_0_L, upsample_buffer_1_L, &biquad_filter1L);
                len_L = fast_BQ_filter_2x_2(len_L, upsample_buffer_1_L, upsample_buffer_0_L, &biquad_filter2L);

                len_R = fast_BQ_filter_2x_0(length, buffer_from_ep_Rch_float, upsample_buffer_0_R, &biquad_filter0R);
                len_R = fast_BQ_filter_2x_1(len_R, upsample_buffer_0_R, upsample_buffer_1_R, &biquad_filter1R);
                len_R = fast_BQ_filter_2x_2(len_R, upsample_buffer_1_R, upsample_buffer_0_R, &biquad_filter2R);
            }
            else
            {
                len_L = fast_BQ_filter_2x_0(length, buffer_from_ep_Lch_float, upsample_buffer_1_L, &biquad_filter0L);
                len_L = fast_BQ_filter_2x_1(len_L, upsample_buffer_1_L, upsample_buffer_0_L, &biquad_filter1L);

                len_R = fast_BQ_filter_2x_0(length, buffer_from_ep_Rch_float, upsample_buffer_1_R, &biquad_filter0R);
                len_R = fast_BQ_filter_2x_1(len_R, upsample_buffer_1_R, upsample_buffer_0_R, &biquad_filter1R);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_0_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock((int32_t*)upsample_buffer_0_R, len_R, &buffer_upsr_data_Rch_0);
            restore_interrupts(save);
        }
        break;
    }
}

uint32_t __not_in_flash_func(upsampling_process_core1)(float *in_L, float *in_R, float *out_L, float *out_R, uint32_t length)
{
    uint32_t len_L = length;
    switch (get_ratio_upsampling_core1())
    {
    case 4:
        len_L = fast_BQ_filter_4x_0(length, in_L, out_L, &biquad_filter4L);
        fast_BQ_filter_4x_0(length, in_R, out_R, &biquad_filter4R);
        break;

    case 2:
        len_L = fast_BQ_filter_2x_3(length, in_L, out_L, &biquad_filter3L);
        fast_BQ_filter_2x_3(length, in_R, out_R, &biquad_filter3R);
        break;

    case 1:
    default:
        memcpy(out_L, in_L, sizeof(float) * length);
        memcpy(out_R, in_R, sizeof(float) * length);
        break;
    }
    return len_L;
}