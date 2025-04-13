/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "upsampling.h"
#include "debug_with_gpio.h"
#include "ringbuffer.h"
#include "common.h"

// 遅延バッファ
DELAY_DATA bq_delay_L;
DELAY_DATA bq_delay_R;

// 双二次フィルタ構造体
BIQUAD_FILTER bq_filter_0[SIZE_BQ_FILTER_0];
BIQUAD_FILTER bq_filter_1[SIZE_BQ_FILTER_1];
BIQUAD_FILTER bq_filter_2[SIZE_BQ_FILTER_2];
BIQUAD_FILTER bq_filter_3[SIZE_BQ_FILTER_3];
BIQUAD_FILTER bq_filter_4[SIZE_BQ_FILTER_4];

// BiQuad-IIRフィルタの係数を初期化する
static void initialize_bq_filter_coef(void)
{
    for (uint16_t i = 0; i < SIZE_BQ_FILTER_0; i++)
    {
        bq_filter_0[i].a0 = coef_bq_filter_2x_0[i][0];
        bq_filter_0[i].a1 = coef_bq_filter_2x_0[i][1];
        bq_filter_0[i].a2 = coef_bq_filter_2x_0[i][2];
        bq_filter_0[i].b1 = coef_bq_filter_2x_0[i][4];
        bq_filter_0[i].b2 = coef_bq_filter_2x_0[i][5];
    }
    for (uint16_t i = 0; i < SIZE_BQ_FILTER_1; i++)
    {
        bq_filter_1[i].a0 = coef_bq_filter_2x_1[i][0];
        bq_filter_1[i].a1 = coef_bq_filter_2x_1[i][1];
        bq_filter_1[i].a2 = coef_bq_filter_2x_1[i][2];
        bq_filter_1[i].b1 = coef_bq_filter_2x_1[i][4];
        bq_filter_1[i].b2 = coef_bq_filter_2x_1[i][5];
    }

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_2; i++)
    {
        bq_filter_2[i].a0 = coef_bq_filter_2x_2[i][0];
        bq_filter_2[i].a1 = coef_bq_filter_2x_2[i][1];
        bq_filter_2[i].a2 = coef_bq_filter_2x_2[i][2];
        bq_filter_2[i].b1 = coef_bq_filter_2x_2[i][4];
        bq_filter_2[i].b2 = coef_bq_filter_2x_2[i][5];
    }

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_3; i++)
    {
        bq_filter_3[i].a0 = coef_bq_filter_2x_3[i][0];
        bq_filter_3[i].a1 = coef_bq_filter_2x_3[i][1];
        bq_filter_3[i].a2 = coef_bq_filter_2x_3[i][2];
        bq_filter_3[i].b1 = coef_bq_filter_2x_3[i][4];
        bq_filter_3[i].b2 = coef_bq_filter_2x_3[i][5];
    }

    for (uint16_t i = 0; i < SIZE_BQ_FILTER_4; i++)
    {
        bq_filter_4[i].a0 = coef_bq_filter_4x_0[i][0];
        bq_filter_4[i].a1 = coef_bq_filter_4x_0[i][1];
        bq_filter_4[i].a2 = coef_bq_filter_4x_0[i][2];
        bq_filter_4[i].b1 = coef_bq_filter_4x_0[i][4];
        bq_filter_4[i].b2 = coef_bq_filter_4x_0[i][5];
    }
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
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_0; i++)
    {
        bq_delay_L.delay0[i].z1 = 0;
        bq_delay_L.delay0[i].z2 = 0;
        bq_delay_R.delay0[i].z1 = 0;
        bq_delay_R.delay0[i].z2 = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_1; i++)
    {
        bq_delay_L.delay1[i].z1 = 0;
        bq_delay_L.delay1[i].z2 = 0;
        bq_delay_R.delay1[i].z1 = 0;
        bq_delay_R.delay1[i].z2 = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_2; i++)
    {
        bq_delay_L.delay2[i].z1 = 0;
        bq_delay_L.delay2[i].z2 = 0;
        bq_delay_R.delay2[i].z1 = 0;
        bq_delay_R.delay2[i].z2 = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_3; i++)
    {
        bq_delay_L.delay3[i].z1 = 0;
        bq_delay_L.delay3[i].z2 = 0;
        bq_delay_R.delay3[i].z1 = 0;
        bq_delay_R.delay3[i].z2 = 0;
    }
    for (uint16_t i = 0; i < SIZE_BQ_DELAY_4; i++)
    {
        bq_delay_L.delay4[i].z1 = 0;
        bq_delay_L.delay4[i].z2 = 0;
        bq_delay_R.delay4[i].z1 = 0;
        bq_delay_R.delay4[i].z2 = 0;
    }
}

// upampling biquad IIR filter 高速版コア処理 (絶対にインライン展開しろ)
static __force_inline float BQ_filter_process_f32(BIQUAD_FILTER *filter, float input, BQ_DELAY *delay)
{
    float output = filter->a0 * input + delay->z1;

    delay->z1 = filter->a1 * input + delay->z2 - filter->b1 * output;
    delay->z2 = filter->a2 * input - filter->b2 * output;
    return output;
}

// upsampling FIR
static __force_inline uint32_t FIR_filter_2ch(float *input, uint32_t length, float *output, float *delay, const float *h, const uint16_t size_tap)
{
    int count_out_size = 0;
    for (int sample = 0; sample < length; sample++)
    {
        float temporary = 0;

        *delay = input[sample];

        for (int tap = 0; tap < size_tap; tap++)
            temporary += h[tap] * (*(delay + tap));

        for (int tap = size_tap - 1; tap > 0; tap--)
            *(delay + tap) = *(delay + tap - 1);

        output[sample] = temporary;
        count_out_size++;
    }
    return count_out_size;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_0)(uint32_t length, int32_t *p_in, int32_t *p_out, BQ_DELAY *delay)
{
    uint32_t length_buffer = length;
    int32_t *NOS_buffer = (int32_t *)malloc(sizeof(int32_t) * (length << 1));
    int32_t *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // ここからBiQuad-IIRフィルタを実行する
    for (int32_t sample = 0; sample < (length << 1); sample++)
    {
        float temporary = (float)NOS_buffer[sample];

        temporary = BQ_filter_process_f32(&bq_filter_0[0], temporary, delay);
        temporary = BQ_filter_process_f32(&bq_filter_0[1], temporary, delay + 1);
        temporary = BQ_filter_process_f32(&bq_filter_0[2], temporary, delay + 2);
        temporary = BQ_filter_process_f32(&bq_filter_0[3], temporary, delay + 3);
        temporary = BQ_filter_process_f32(&bq_filter_0[4], temporary, delay + 4);
        temporary = BQ_filter_process_f32(&bq_filter_0[5], temporary, delay + 5);
        temporary = BQ_filter_process_f32(&bq_filter_0[6], temporary, delay + 6);
        temporary = BQ_filter_process_f32(&bq_filter_0[7], temporary, delay + 7);
        temporary = BQ_filter_process_f32(&bq_filter_0[8], temporary, delay + 8);
        temporary = BQ_filter_process_f32(&bq_filter_0[9], temporary, delay + 9);
        temporary = BQ_filter_process_f32(&bq_filter_0[10], temporary, delay + 10);
        temporary = BQ_filter_process_f32(&bq_filter_0[11], temporary, delay + 11);
        temporary = BQ_filter_process_f32(&bq_filter_0[12], temporary, delay + 12);
        temporary = BQ_filter_process_f32(&bq_filter_0[13], temporary, delay + 13);
        temporary = BQ_filter_process_f32(&bq_filter_0[14], temporary, delay + 14);
        temporary = BQ_filter_process_f32(&bq_filter_0[15], temporary, delay + 15);
        temporary = BQ_filter_process_f32(&bq_filter_0[16], temporary, delay + 16);

        p_out[sample] = (int32_t)temporary;
    }
    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_1)(uint32_t length, int32_t *p_in, int32_t *p_out, BQ_DELAY *delay)
{
    uint32_t length_buffer = length;
    int32_t *NOS_buffer = (int32_t *)malloc(sizeof(int32_t) * (length << 1));
    int32_t *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // ここからBiQuad-IIRフィルタを実行する
    for (int32_t sample = 0; sample < (length << 1); sample++)
    {
        float temporary = (float)NOS_buffer[sample];

        temporary = BQ_filter_process_f32(&bq_filter_1[0], temporary, delay);
        temporary = BQ_filter_process_f32(&bq_filter_1[1], temporary, delay + 1);
        temporary = BQ_filter_process_f32(&bq_filter_1[2], temporary, delay + 2);
        temporary = BQ_filter_process_f32(&bq_filter_1[3], temporary, delay + 3);
        temporary = BQ_filter_process_f32(&bq_filter_1[4], temporary, delay + 4);

        p_out[sample] = (int32_t)temporary;
    }
    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_2)(uint32_t length, int32_t *p_in, int32_t *p_out, BQ_DELAY *delay)
{
    uint32_t length_buffer = length;
    int32_t *NOS_buffer = (int32_t *)malloc(sizeof(int32_t) * (length << 1));
    int32_t *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // ここからBiQuad-IIRフィルタを実行する
    for (int32_t sample = 0; sample < (length << 1); sample++)
    {
        float temporary = (float)NOS_buffer[sample];

        temporary = BQ_filter_process_f32(&bq_filter_2[0], temporary, delay);
        temporary = BQ_filter_process_f32(&bq_filter_2[1], temporary, delay + 1);
        temporary = BQ_filter_process_f32(&bq_filter_2[2], temporary, delay + 2);
        temporary = BQ_filter_process_f32(&bq_filter_2[3], temporary, delay + 3);

        p_out[sample] = (int32_t)temporary;
    }
    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_2x_3)(uint32_t length, int32_t *p_in, int32_t *p_out, BQ_DELAY *delay)
{
    uint32_t length_buffer = length;
    int32_t *NOS_buffer = (int32_t *)malloc(sizeof(int32_t) * (length << 1));
    int32_t *p_NOS_buffer = NOS_buffer;

    // サンプル数を2倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // ここからBiQuad-IIRフィルタを実行する
    for (int32_t sample = 0; sample < (length << 1); sample++)
    {
        float temporary = (float)NOS_buffer[sample];

        temporary = BQ_filter_process_f32(&bq_filter_3[0], temporary, delay);
        temporary = BQ_filter_process_f32(&bq_filter_3[1], temporary, delay + 1);
        temporary = BQ_filter_process_f32(&bq_filter_3[2], temporary, delay + 2);
        temporary = BQ_filter_process_f32(&bq_filter_2[3], temporary, delay + 3);

        p_out[sample] = (int32_t)temporary;
    }
    free(NOS_buffer);
    return length << 1;
}

// upsampling biquad IIR filter NOS統合版 (RAM上で実行する)
static uint32_t __not_in_flash_func(fast_BQ_filter_4x_0)(uint32_t length, int32_t *p_in, int32_t *p_out, BQ_DELAY *delay)
{
    uint32_t length_buffer = length;
    int32_t *NOS_buffer = (int32_t *)malloc(sizeof(int32_t) * (length << 2));
    int32_t *p_NOS_buffer = NOS_buffer;

    // サンプル数を4倍にする NOS方式
    while (length_buffer--)
    {
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in);
        *(p_NOS_buffer++) = *(p_in++);
    }

    // ここからBiQuad-IIRフィルタを実行する
    for (int32_t sample = 0; sample < (length << 2); sample++)
    {
        float temporary = (float)NOS_buffer[sample];

        temporary = BQ_filter_process_f32(&bq_filter_4[0], temporary, delay);
        temporary = BQ_filter_process_f32(&bq_filter_4[1], temporary, delay + 1);
        temporary = BQ_filter_process_f32(&bq_filter_4[2], temporary, delay + 2);

        p_out[sample] = (int32_t)temporary;
    }
    free(NOS_buffer);
    return length << 2;
}

// アップサンプリングに使用するメモリを静的確保
static int32_t buffer_copy_from_ep_left_ch[SIZE_EP_BUFFER];
static int32_t buffer_copy_from_ep_right_ch[SIZE_EP_BUFFER];
static int32_t upsample_buffer_0_L[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static int32_t upsample_buffer_1_L[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static int32_t upsample_buffer_0_R[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];
static int32_t upsample_buffer_1_R[SIZE_EP_BUFFER * RATIO_UPSAMPLING_48K];

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

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_2(length, buffer_copy_from_ep_left_ch, upsample_buffer_0_L, bq_delay_L.delay2);
                len_R = fast_BQ_filter_2x_2(length, buffer_copy_from_ep_right_ch, upsample_buffer_0_R, bq_delay_R.delay2);
            }
            else
            {
                len_L = length;
                len_R = length;
                memcpy(buffer_copy_from_ep_left_ch, upsample_buffer_0_L, sizeof(int32_t) * length);
                memcpy(buffer_copy_from_ep_right_ch, upsample_buffer_0_R, sizeof(int32_t) * length);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock(upsample_buffer_0_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock(upsample_buffer_0_R, len_R, &buffer_upsr_data_Rch_0);
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

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_1(length, buffer_copy_from_ep_left_ch, upsample_buffer_0_L, bq_delay_L.delay1);
                len_L = fast_BQ_filter_2x_2(len_L, upsample_buffer_0_L, upsample_buffer_1_L, bq_delay_L.delay2);

                len_R = fast_BQ_filter_2x_1(length, buffer_copy_from_ep_right_ch, upsample_buffer_0_R, bq_delay_R.delay1);
                len_R = fast_BQ_filter_2x_2(len_R, upsample_buffer_0_R, upsample_buffer_1_R, bq_delay_R.delay2);
            }
            else
            {
                len_L = fast_BQ_filter_2x_1(length, buffer_copy_from_ep_left_ch, upsample_buffer_1_L, bq_delay_L.delay1);
                len_R = fast_BQ_filter_2x_1(length, buffer_copy_from_ep_right_ch, upsample_buffer_1_R, bq_delay_R.delay1);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock(upsample_buffer_1_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock(upsample_buffer_1_R, len_R, &buffer_upsr_data_Rch_0);
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

            if (!CORE0_UPSAMPLING_192K)
            {
                len_L = fast_BQ_filter_2x_0(length, buffer_copy_from_ep_left_ch, upsample_buffer_0_L, bq_delay_L.delay0);
                len_L = fast_BQ_filter_2x_1(len_L, upsample_buffer_0_L, upsample_buffer_1_L, bq_delay_L.delay1);
                len_L = fast_BQ_filter_2x_2(len_L, upsample_buffer_1_L, upsample_buffer_0_L, bq_delay_L.delay2);

                len_R = fast_BQ_filter_2x_0(length, buffer_copy_from_ep_right_ch, upsample_buffer_0_R, bq_delay_R.delay0);
                len_R = fast_BQ_filter_2x_1(len_R, upsample_buffer_0_R, upsample_buffer_1_R, bq_delay_R.delay1);
                len_R = fast_BQ_filter_2x_2(len_R, upsample_buffer_1_R, upsample_buffer_0_R, bq_delay_R.delay2);
            }
            else
            {
                len_L = fast_BQ_filter_2x_0(length, buffer_copy_from_ep_left_ch, upsample_buffer_1_L, bq_delay_L.delay0);
                len_L = fast_BQ_filter_2x_1(len_L, upsample_buffer_1_L, upsample_buffer_0_L, bq_delay_L.delay1);

                len_R = fast_BQ_filter_2x_0(length, buffer_copy_from_ep_right_ch, upsample_buffer_1_R, bq_delay_R.delay0);
                len_R = fast_BQ_filter_2x_1(len_R, upsample_buffer_1_R, upsample_buffer_0_R, bq_delay_R.delay1);
            }

            save = save_and_disable_interrupts();
            ringbuf_write_array_spinlock(upsample_buffer_0_L, len_L, &buffer_upsr_data_Lch_0);
            ringbuf_write_array_spinlock(upsample_buffer_0_R, len_R, &buffer_upsr_data_Rch_0);
            restore_interrupts(save);
        }
        break;
    }
}

uint32_t __not_in_flash_func(upsampling_process_core1)(int32_t *in_L, int32_t *in_R, int32_t *out_L, int32_t *out_R, uint32_t length)
{
    uint32_t len_L = length;
    switch (get_ratio_upsampling_core1())
    {
    case 4:
        len_L = fast_BQ_filter_4x_0(length, in_L, out_L, bq_delay_L.delay4);
        fast_BQ_filter_4x_0(length, in_R, out_R, bq_delay_R.delay4);
        break;

    case 2:
        len_L = fast_BQ_filter_2x_3(length, in_L, out_L, bq_delay_L.delay3);
        fast_BQ_filter_2x_3(length, in_R, out_R, bq_delay_R.delay3);
        break;

    case 1:
    default:
        memcpy(out_L, in_L, sizeof(int32_t) * length);
        memcpy(out_R, in_R, sizeof(int32_t) * length);
        break;
    }
    return len_L;
}