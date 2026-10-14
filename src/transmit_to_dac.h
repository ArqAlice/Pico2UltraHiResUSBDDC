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

#define SIZE_DMA_TX_BUF (49 * 2 * CORE0_UP_RATIO_MAX * CORE1_UP_RATIO_MAX * CORE1_PROCESS_US / 1000 + 256)

// DMA転送バッファはダブルバッファとして使うので2で十分
#define SIZE_DMA_TX_BUF_STACK (DEPTH_DMA_TX_BUFFER)

typedef struct
{
    uint32_t tx_buf[SIZE_DMA_TX_BUF];
    uint32_t tx_size;
} DMA_TX_DATA;

typedef struct
{
    DMA_TX_DATA data[DEPTH_DMA_TX_BUFFER];
    volatile uint32_t wp;
    volatile uint32_t rp;
    volatile uint32_t using;
    volatile uint32_t prev_write_length;
} DMA_TX_STRUCTURE;

extern void init_i2s_interface(void);
extern void reset_i2s_freq(void);
extern void __not_in_flash_func(dma_tx_start)(void);
extern void dma_stop_and_clear(void);
extern void pwm_i2s_streaming_rate_change(void);
extern void set_pwm_isr_1(float period_us);

// Low-power idle support: shared output-running flag + quiesced predicate.
// enable_output is written by Core1 (dma_tx_start) and read by Core0 (depop timing).
extern volatile bool enable_output;

// Returns true when output is fully stopped (no frame enabled and no DMA in flight).
// Used by Core1 to decide it is safe to enter WFI.
extern bool dma_tx_is_quiesced(void);

// Returns true when playback is running but there is nothing to submit right now
// (TX ring already full). Core1 may briefly WFI (poll ~CORE1_WFI_PLAY_US) instead
// of busy-waiting; a chunk is guaranteed to complete within CORE1_PROCESS_US, so
// re-arming within 250us cannot cause an underrun.
extern bool dma_tx_no_submit_pending(void);

#endif /* _TRANSMIT_TO_DAC_H_ */