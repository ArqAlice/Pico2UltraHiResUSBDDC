/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _NONBLOCKING_I2C_H_
#define _NONBLOCKING_I2C_H_

#include "hardware/i2c.h"

typedef struct RB_I2C
{
    volatile uint16_t size_buffer;
    volatile uint16_t write_point;
    volatile uint16_t read_point;
    volatile int16_t size_using;
    uint8_t *buffer;
} I2C_RINGBUFFER;

extern void i2c_dma_initialize(i2c_inst_t *i2c);
extern bool i2c_dma_is_busy(void);
extern void i2c_write_dma(i2c_inst_t *i2c_inst, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop);

extern int16_t initialize_i2c_ringbuffer(uint16_t size, I2C_RINGBUFFER *ringbuffer);
extern int64_t i2c_ringbuf_get_size_using(I2C_RINGBUFFER *ringbuffer);
extern int16_t i2c_ringbuf_write(uint8_t input, I2C_RINGBUFFER *ringbuffer);
extern int16_t i2c_ringbuf_read(uint8_t *output, I2C_RINGBUFFER *ringbuffer);
extern int64_t i2c_ringbuf_read_array(uint8_t *output, uint32_t size, I2C_RINGBUFFER *ringbuffer);
extern int64_t i2c_ringbuf_write_array(uint8_t *input, uint32_t size, I2C_RINGBUFFER *ringbuffer);

#endif