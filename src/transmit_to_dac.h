/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _TRANSMIT_TO_DAC_H_
#define _TRANSMIT_TO_DAC_H_

#include "pico/stdlib.h"
#include "common.h"
#include "upsampling.h"

#define SIZE_DMA_TX_BUF (49 * 2 * RATIO_UPSAMPLING_48K * RATIO_UPSAMPLING_CORE1 * TIMER_US_CORE1 / 1000 + 256)

// DMA転送バッファはダブルバッファとして使うので2で十分
#define SIZE_DMA_TX_BUF_STACK 2

typedef struct
{
    uint32_t tx_buf[SIZE_DMA_TX_BUF];
    uint32_t tx_size;
} DMA_TX_DATA;

typedef struct
{
    DMA_TX_DATA data[DEPTH_DMA_TX_BUFFER];
    uint32_t wp;
    uint32_t rp;
    uint32_t using;
    uint32_t prev_write_length;
} DMA_TX_STRUCTURE;

extern void init_i2s_interface(void);
extern void reset_i2s_freq(void);
extern void __not_in_flash_func(dma_tx_start)(void);
extern void dma_stop_and_clear(void);
extern void pwm_i2s_streaming_rate_change(void);
extern void set_pwm_isr_1(float period_us);

#endif /* _TRANSMIT_TO_DAC_H_ */