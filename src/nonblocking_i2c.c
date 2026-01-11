/*
 * Copyright (c) 2025 ArqAlice
 *
 * Released under the MIT license
 * https://opensource.org/licenses/mit-license.php
 */

#include "nonblocking_i2c.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"

#define bool_to_bit(x) ((uint) !!(x))

static bool is_transferring_data;
static i2c_hw_t *i2c_hw;
static int dma_ch;
static dma_channel_config c_dma;
static uint32_t i2c_tx_cmd_data[16];
static uint16_t tx_length = 0;

void dma_i2c_tx_irq_handler(void);
void i2c_irq_handler(void);

void i2c_dma_initialize(i2c_inst_t *i2c)
{
    dma_ch = dma_claim_unused_channel(true);
    // dma_channel_set_irq1_enabled(dma_ch, true);                   // DMA TX チャネルで IRQ0 有効化
    // irq_set_exclusive_handler(DMA_IRQ_1, dma_i2c_tx_irq_handler); // 割り込み関数登録
    // irq_set_enabled(DMA_IRQ_1, true);                             // NVIC有効化
    // irq_set_priority(DMA_IRQ_1, 0);                               // 最優先で割り込み

    c_dma = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c_dma, DMA_SIZE_32);
    channel_config_set_read_increment(&c_dma, true);
    channel_config_set_write_increment(&c_dma, false);
    channel_config_set_dreq(&c_dma, i2c_get_dreq(i2c, true));
}

bool i2c_dma_is_busy(void)
{
    return is_transferring_data;
}

void i2c_write_dma(i2c_inst_t *i2c, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop)
{
    tx_length = len;
    if (len == 0 || data == NULL)
        return;

    // dma動作中は終わるまで待機
    while (dma_channel_is_busy(dma_ch))
        tight_loop_contents();

    // I2Cのアドレスを格納し、割り込み処理をセット
    if (i2c == i2c0)
    {
        is_transferring_data = true;
        i2c_hw = i2c0_hw;
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_STOP_DET_BITS; // STOP_DET割り込みを有効化
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_EMPTY_BITS; // TX_EMPTY割り込みを有効化
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_ABRT_BITS;  // TX_ABRT割り込みを有効化
        irq_set_exclusive_handler(I2C0_IRQ, i2c_irq_handler);
        irq_set_enabled(I2C0_IRQ, true);
        irq_set_priority(I2C0_IRQ, 0);
    }
    else if (i2c == i2c1)
    {
        is_transferring_data = true;
        i2c_hw = i2c1_hw;
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_STOP_DET_BITS; // STOP_DET割り込みを有効化
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_EMPTY_BITS; // TX_EMPTY割り込みを有効化
        i2c_hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_ABRT_BITS;  // TX_ABRT割り込みを有効化
        irq_set_exclusive_handler(I2C1_IRQ, i2c_irq_handler);
        irq_set_enabled(I2C1_IRQ, true);
        irq_set_priority(I2C1_IRQ, 0);
    }
    else
    {
        return; // 不正なI2Cを指定したらreturnする
    }

    // コマンドデータを作成
    for (int byte_ctr = 0; byte_ctr < (int)len; byte_ctr++)
    {
        bool first = (byte_ctr == 0);
        bool last = (byte_ctr == len - 1);

        i2c_tx_cmd_data[byte_ctr] =
            bool_to_bit(first && i2c->restart_on_next) << I2C_IC_DATA_CMD_RESTART_LSB |
            bool_to_bit(last && !nostop) << I2C_IC_DATA_CMD_STOP_LSB |
            *data++;
    }

    // I2C無効化 → アドレス設定 → 有効化
    i2c_hw->enable = 0;
    i2c_hw->tar = addr_7bit; // 7bitアドレス（左にシフトしない）
    i2c_hw->enable = 1;

    // DMAで先頭〜(n-2)バイト送信
    dma_channel_configure(
        dma_ch,
        &c_dma,
        &i2c_hw->data_cmd,       // 書き込み先
        (void *)i2c_tx_cmd_data, // 読み込み元
        len,
        true // 即開始
    );

    // nostop means we are now at the end of a *message* but not the end of a *transfer*
    i2c->restart_on_next = nostop;
}

void dma_i2c_tx_irq_handler(void)
{
    // 最後の1バイトを STOP ビット付きで送る
    // i2c_hw->data_cmd = i2c_tx_cmd_data[tx_length - 1] | I2C_IC_DATA_CMD_STOP_BITS;

    dma_hw->ints1 = 1u << dma_ch; // 割り込みフラグクリア
}

void i2c_irq_handler(void)
{
    i2c_hw->clr_stop_det; // STOP割り込みフラグをクリア
    i2c_hw->clr_tx_abrt;  // Abort割り込みフラグをクリア

    // I2Cの割り込み処理の割付を解除
    i2c_hw->intr_mask &= ~I2C_IC_INTR_MASK_M_STOP_DET_BITS; // STOP_DET割り込みを無効化
    i2c_hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS; // TX_EMPTY割り込みを無効化
    i2c_hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_ABRT_BITS;  // TX_ABRT割り込みを無効化

    if (i2c_hw == i2c0_hw)
    {
        irq_remove_handler(I2C0_IRQ, i2c_irq_handler);
        irq_set_enabled(I2C0_IRQ, false);
    }
    else if (i2c_hw == i2c1_hw)
    {
        irq_remove_handler(I2C1_IRQ, i2c_irq_handler);
        irq_set_enabled(I2C1_IRQ, false);
    }
    is_transferring_data = false;
}

// ---------------------- ring buffer ----------------------
extern int16_t initialize_i2c_ringbuffer(uint16_t size, I2C_RINGBUFFER *ringbuffer)
{
    ringbuffer->size_buffer = size;
    ringbuffer->write_point = 0;
    ringbuffer->read_point = 0;
    ringbuffer->size_using = 0;
    ringbuffer->buffer = (I2C_RB_DATA *)malloc(sizeof(I2C_RB_DATA) * size);
    return 0;
}

extern void clear_i2c_ringbuffer(I2C_RINGBUFFER *ringbuffer)
{
    ringbuffer->write_point = 0;
    ringbuffer->read_point = 0;
    ringbuffer->size_using = 0;
}

extern bool i2c_ringbuf_is_full(I2C_RINGBUFFER *ringbuffer)
{
    if (ringbuffer->write_point - ringbuffer->read_point >= ringbuffer->size_buffer)
        return true;
    else
        return false;
}

extern int64_t i2c_ringbuf_get_size_using(I2C_RINGBUFFER *ringbuffer)
{
    return ringbuffer->size_using;
}

extern int64_t i2c_ringbuf_get_size_remain(I2C_RINGBUFFER *ringbuffer)
{
    return ringbuffer->size_buffer - ringbuffer->size_using;
}

extern uint32_t i2c_ringbuf_get_read_point(I2C_RINGBUFFER *ringbuffer)
{
    return ringbuffer->read_point;
}

extern uint32_t i2c_ringbuf_get_write_point(I2C_RINGBUFFER *ringbuffer)
{
    return ringbuffer->write_point;
}

extern int16_t i2c_ringbuf_write(I2C_RB_DATA *input, I2C_RINGBUFFER *ringbuffer)
{
    if (ringbuffer->size_using == ringbuffer->size_buffer)
    {
        return -1; // buffer is full
    }

    memcpy(ringbuffer->buffer + (ringbuffer->write_point), input, sizeof(I2C_RB_DATA));
    ringbuffer->write_point++;
    if (ringbuffer->write_point >= ringbuffer->size_buffer)
        ringbuffer->write_point = 0;
    ringbuffer->size_using++;
    return 1;
}

extern int16_t i2c_ringbuf_read(I2C_RB_DATA *output, I2C_RINGBUFFER *ringbuffer)
{
    if (ringbuffer->size_using == 0)
    {
        return -1; // buffer is empty
    }

    memcpy(output, ringbuffer->buffer + (ringbuffer->read_point), sizeof(I2C_RB_DATA));
    ringbuffer->read_point++;
    if (ringbuffer->read_point >= ringbuffer->size_buffer)
        ringbuffer->read_point = 0;
    ringbuffer->size_using--;
    return 1;
}

void i2c_ringbuf_set_data(i2c_inst_t *i2c, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop, I2C_RB_DATA *output)
{
    output->i2c = i2c;
    output->addr_7bit = addr_7bit;
    memcpy(output->data, data, len * sizeof(uint8_t));
    output->len = (uint8_t)len;
    output->nostop = nostop;
}

void i2c_dma_stop_and_clear(void)
{
    // DMAを強制停止
    dma_channel_abort(dma_ch);

    // DMAステータスをリセット
    dma_hw->ints1 = (1u << dma_ch);

    is_transferring_data = false;
}