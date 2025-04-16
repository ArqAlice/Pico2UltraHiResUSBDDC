/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _UPSAMPLING_H_
#define _UPSAMPLING_H_

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"

#define NUM_OF_CH (2)
#define NUM_OF_BQ_SUB_PARAMS (6)
#define SIZE_BQ_FILTER_0 (17)
#define SIZE_BQ_DELAY_0 (SIZE_BQ_FILTER_0)

#define SIZE_BQ_FILTER_1 (5)
#define SIZE_BQ_DELAY_1 (SIZE_BQ_FILTER_1)

#define SIZE_BQ_FILTER_2 (4)
#define SIZE_BQ_DELAY_2 (SIZE_BQ_FILTER_2)

#define SIZE_BQ_FILTER_3 (4)
#define SIZE_BQ_DELAY_3 (SIZE_BQ_FILTER_3)

#define SIZE_BQ_FILTER_4 (3)
#define SIZE_BQ_DELAY_4 (SIZE_BQ_FILTER_4)

#define SIZE_FIR_FILTER_0 (128)
#define SIZE_FIR_FILTER_1 (48)
#define SIZE_FIR_FILTER_2 (48)


// 双二次フィルタの係数と遅延を定義する
typedef struct
{
    float a0, a1, a2;
    float b1, b2;
} BIQUAD_FILTER;

typedef struct
{
    float z1, z2;
} BQ_DELAY;

typedef struct
{
    BQ_DELAY delay0[SIZE_BQ_DELAY_0];
    BQ_DELAY delay1[SIZE_BQ_DELAY_1];
    BQ_DELAY delay2[SIZE_BQ_DELAY_2];
    BQ_DELAY delay3[SIZE_BQ_DELAY_3];
    BQ_DELAY delay4[SIZE_BQ_DELAY_4];
} DELAY_DATA;

// フィルタ係数
extern const float coef_bq_filter_2x_0[SIZE_BQ_FILTER_0][NUM_OF_BQ_SUB_PARAMS];
extern const float coef_bq_filter_2x_1[SIZE_BQ_FILTER_1][NUM_OF_BQ_SUB_PARAMS];
extern const float coef_bq_filter_2x_2[SIZE_BQ_FILTER_2][NUM_OF_BQ_SUB_PARAMS];
extern const float coef_bq_filter_2x_3[SIZE_BQ_FILTER_3][NUM_OF_BQ_SUB_PARAMS];
extern const float coef_bq_filter_4x_0[SIZE_BQ_FILTER_4][NUM_OF_BQ_SUB_PARAMS];

extern const float coef_fir_filter_4x_0[SIZE_FIR_FILTER_0];
extern const uint32_t size_coef_fir_filter_4x_0;
extern const float coef_fir_filter_2x_1[SIZE_FIR_FILTER_1];
extern const uint32_t size_coef_fir_filter_2x_1;
extern const float coef_fir_filter_2x_2[SIZE_FIR_FILTER_2];
extern const uint32_t size_coef_fir_filter_2x_2;


extern void init_upsampling_filter(void);
extern void clear_bq_filter_delay(void);
extern void __not_in_flash_func(upsampling_process_core0)(void);
extern uint32_t __not_in_flash_func(upsampling_process_core1)(float *in_L, float *in_R, float *out_L, float *out_R, uint32_t length);

#endif /* _UPSAMPLING_H_ */