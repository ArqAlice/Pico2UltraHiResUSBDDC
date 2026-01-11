/*
 * Copyright (c) 2025 ArqAlice
 *
 * Released under the MIT license
 * https://opensource.org/licenses/mit-license.php
 */

#ifndef _NONBLOCKING_I2C_H_
#define _NONBLOCKING_I2C_H_

#include "hardware/i2c.h"
#include "common.h"

typedef struct I2C_RB_DATA
{
    i2c_inst_t *i2c;
    uint8_t addr_7bit;
    uint8_t data[SIZE_I2C_TRANSFER_MAX];
    uint8_t len;
    bool nostop;
} I2C_RB_DATA;

typedef struct RB_I2C
{
    volatile uint16_t size_buffer;
    volatile uint16_t write_point;
    volatile uint16_t read_point;
    volatile int16_t size_using;
    I2C_RB_DATA *buffer;
} I2C_RINGBUFFER;

extern void i2c_dma_initialize(i2c_inst_t *i2c);
extern bool i2c_dma_is_busy(void);
extern void i2c_write_dma(i2c_inst_t *i2c_inst, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop);

extern int16_t initialize_i2c_ringbuffer(uint16_t size, I2C_RINGBUFFER *ringbuffer);
extern int64_t i2c_ringbuf_get_size_using(I2C_RINGBUFFER *ringbuffer);
extern int16_t i2c_ringbuf_write(I2C_RB_DATA *input, I2C_RINGBUFFER *ringbuffer);
extern int16_t i2c_ringbuf_read(I2C_RB_DATA *output, I2C_RINGBUFFER *ringbuffer);
extern void i2c_ringbuf_set_data(i2c_inst_t *i2c, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop, I2C_RB_DATA *output);
extern void i2c_dma_stop_and_clear(void);

#endif