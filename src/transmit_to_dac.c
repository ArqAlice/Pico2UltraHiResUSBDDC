/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <string.h>
#include "transmit_to_dac.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "i2s_pio_interface.h"
#include "upsampling.h"

static const PIO pio = pio0;
static const uint sm = 0;
static pio_sm_config sm_config;
static uint offset;

static volatile int dma_ch;
static dma_channel_config c_dma;
static volatile bool dma_using_buffer_A = true;
DMA_TX_STRUCTURE dma_tx;

static bool enable_output = false;
static bool is_enabled_dma_tx_isr = false;

// PWM
static uint16_t pwm_slice;

void __not_in_flash_func(dma_tx_irq_handler)(void);
void __not_in_flash_func(i2s_tx_process)(void);
void __not_in_flash_func(dma_tx_start)(void);

bool calc_pwm_clkdiv_and_wrap_us(float target_period_us, uint16_t *wrap_out, float *clkdiv_out);
void set_pwm_isr_1(float period_us);
void set_period_pwm_us(float period_us);
void pwm_i2s_streaming_rate_change(void);
void __not_in_flash_func(pwm_wrap_handler)(void);

void reset_i2s_freq(void)
{
    I2S_freq_init(pio, sm, &sm_config, offset);
}

void init_i2s_interface(void)
{
#if I2S_SLEWRATE_FAST_ENABLE == true
    // PIO I2S用のGPIOを高速化する
    gpio_set_slew_rate(I2S_DATA_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(I2S_SIDESET_BASE + 1, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(I2S_SIDESET_BASE + 2, GPIO_SLEW_RATE_FAST);
#endif

#if I2S_STRENGTH_REINFORCE_ENABLE == true
    gpio_set_drive_strength(I2S_DATA_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(I2S_SIDESET_BASE, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(I2S_SIDESET_BASE + 1, GPIO_DRIVE_STRENGTH_12MA);
#endif

#if I2S_SIDESET_CHANGE == true
    // PIO I2Sの初期化
    I2S_32bit_inv_program_init(pio, sm, I2S_DATA_PIN, I2S_SIDESET_BASE, AUDIO_INITIAL_FREQ, &sm_config, &offset);
#else
    // PIO I2Sの初期化
    I2S_32bit_program_init(pio, sm, I2S_DATA_PIN, I2S_SIDESET_BASE, AUDIO_INITIAL_FREQ, &sm_config, &offset);
#endif

    reset_i2s_freq();

    // DMA設定
    dma_ch = dma_claim_unused_channel(true);
    dma_channel_set_irq0_enabled(dma_ch, true);               // DMA TX チャネルで IRQ0 有効化
    irq_set_exclusive_handler(DMA_IRQ_0, dma_tx_irq_handler); // 割り込み関数登録
    irq_set_enabled(DMA_IRQ_0, true);                         // NVIC有効化
    irq_set_priority(DMA_IRQ_0, 0);                           // 最優先で割り込み

    c_dma = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c_dma, DMA_SIZE_32);
    channel_config_set_dreq(&c_dma, pio_get_dreq(pio, sm, true));

    // DMAデータ初期化
    memset((void *)&dma_tx, 0, sizeof(DMA_TX_STRUCTURE));

    dma_channel_configure(dma_ch, &c_dma,
                          &pio->txf[sm], dma_tx.data->tx_buf, SIZE_DMA_TX_BUF, false);
}

// DMA TX割り込み処理
void __not_in_flash_func(dma_tx_irq_handler)(void)
{
    i2s_tx_process();
    dma_hw->ints0 = 1u << dma_ch; // 割り込みフラグクリア
}

// I2S送信タイミング通知を行う
void __not_in_flash_func(pwm_wrap_handler)(void)
{
    pwm_clear_irq(pwm_slice); // PWM割り込みフラグをクリア
}

// アップサンプリングに使用するメモリを静的確保
static int32_t from_core0_Lch[SIZE_DMA_TX_BUF / RATIO_UPSAMPLING_CORE1 / 2];
static int32_t from_core0_Rch[SIZE_DMA_TX_BUF / RATIO_UPSAMPLING_CORE1 / 2];
static int32_t upsr_core1_Lch[SIZE_DMA_TX_BUF / 2];
static int32_t upsr_core1_Rch[SIZE_DMA_TX_BUF / 2];

// 転送用バッファを静的確保
static int32_t buffer_i2s_transmit[SIZE_DMA_TX_BUF];

void __not_in_flash_func(dma_tx_start)(void)
{
    int32_t length = get_size_using(&buffer_upsr_data_Lch_0);

    // バッファに規定量以上のデータが溜まってから出力開始
    if (length > SIZE_BUFFER_FB_THRESHOLD)
        enable_output = true;

    // 入ってくるデータが枯渇したうえで、DMA送信バッファ内のすべてのデータを送信完了したら出力停止
    else if ((length <= 0) && (dma_tx.using == 0))
        enable_output = false;

    if (enable_output)
    {
        // 入ってくるデータが枯渇した、またはDMA送信バッファに規定量以上データが蓄積されているときはデータ送信処理をしない
        if ((length > 0) && (dma_tx.using < SIZE_DMA_TX_BUF_STACK))
        {
            int32_t transmit_ref_size = audio_state.freq * get_ratio_upsampling_core0(audio_state.freq) / 1000 * (TIMER_US_CORE1 / 1000.0);
            length = saturation_i32(length, transmit_ref_size, 0);

            ringbuf_read_array_spinlock(from_core0_Lch, length, &buffer_upsr_data_Lch_0);
            ringbuf_read_array_spinlock(from_core0_Rch, length, &buffer_upsr_data_Rch_0);

            // Core1で、さらにアップサンプリングをする(絶対250us以内に終わらせること)
            length = upsampling_process_core1(from_core0_Lch, from_core0_Rch, upsr_core1_Lch, upsr_core1_Rch, length);

            int count = 0;
            for (uint i = 0; i < length; i++)
            {
                buffer_i2s_transmit[count++] = upsr_core1_Lch[i];
                buffer_i2s_transmit[count++] = upsr_core1_Rch[i];
            }

            uint32_t save = save_and_disable_interrupts();
            dma_tx.data[dma_tx.wp].tx_size = count;
            memcpy((void *)dma_tx.data[dma_tx.wp].tx_buf, (void *)buffer_i2s_transmit, sizeof(uint32_t) * dma_tx.data[dma_tx.wp].tx_size);
            dma_tx.wp++;
            if (dma_tx.wp >= DEPTH_DMA_TX_BUFFER)
                dma_tx.wp = 0;
            dma_tx.using ++;
            restore_interrupts(save);

            // 初回はこちらでDMAを起動する(2回目以降はDMA割り込みで自動処理)
            if (dma_tx.prev_write_length == 0)
                is_enabled_dma_tx_isr = false;
            if ((!is_enabled_dma_tx_isr) && (!dma_channel_is_busy(dma_ch)))
            {
                is_enabled_dma_tx_isr = true;
                i2s_tx_process();
            }
        }
    }
    else
    {
        is_enabled_dma_tx_isr = false;
        memset((void *)&dma_tx, 0, sizeof(DMA_TX_STRUCTURE));
    }
}

void __not_in_flash_func(i2s_tx_process)(void)
{
    if (dma_tx.using == DEPTH_DMA_TX_BUFFER)
    {
        // buffer is full
        dma_tx.prev_write_length = 0;
        return;
    }

    if (dma_tx.using == 0)
    {
        // buffer is empty
        dma_tx.prev_write_length = 0;
        return;
    }

    // 転送処理
    dma_channel_set_read_addr(dma_ch, dma_tx.data[dma_tx.rp].tx_buf, false);
    dma_channel_set_trans_count(dma_ch, dma_tx.data[dma_tx.rp].tx_size, true);
    dma_tx.prev_write_length = dma_tx.data[dma_tx.rp].tx_size;

    dma_tx.rp++;
    if (dma_tx.rp >= DEPTH_DMA_TX_BUFFER)
        dma_tx.rp = 0;
    dma_tx.using --;
}

void dma_stop_and_clear(void)
{
    // DMAを強制停止
    dma_channel_abort(dma_ch);

    // DMAステータスをリセット
    dma_hw->ints0 = 1u << dma_ch;

    // FIFOバッファをクリア
    pio_sm_clear_fifos(pio0, 0);

    // 使用バッファをゼロクリア
    memset((void *)&dma_tx, 0, sizeof(DMA_TX_STRUCTURE));

    // DMA停止を通知
    is_enabled_dma_tx_isr = false;
}