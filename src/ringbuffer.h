/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

typedef struct RB
{
    volatile uint32_t size_buffer;
    volatile uint32_t write_point;
    volatile uint32_t read_point;
    volatile int32_t size_using;
    int32_t *buffer;
    spin_lock_t *spinlock;
    bool no_spinlock;
} RINGBUFFER;

extern int16_t initialize_ringbuffer(uint32_t size, bool no_spinlock, RINGBUFFER *ringbuffer);
extern void clear_ringbuffer(RINGBUFFER *ringbuffer);
extern bool __not_in_flash_func(ringbuffer_is_full)(RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(get_size_using)(RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(get_size_remain)(RINGBUFFER *ringbuffer);
extern uint32_t __not_in_flash_func(get_read_point)(RINGBUFFER *ringbuffer);
extern uint32_t __not_in_flash_func(get_write_point)(RINGBUFFER *ringbuffer);
extern int16_t __not_in_flash_func(ringbuf_read_spinlock)(int32_t *output, RINGBUFFER *ringbuffer);
extern int16_t __not_in_flash_func(ringbuf_write_spinlock)(int32_t input, RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(ringbuf_read_array_spinlock)(int32_t *output, uint32_t size, RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(ringbuf_write_array_spinlock)(int32_t *input, uint32_t size, RINGBUFFER *ringbuffer);

extern int16_t __not_in_flash_func(ringbuf_read_no_spinlock)(int32_t *output, RINGBUFFER *ringbuffer);
extern int16_t __not_in_flash_func(ringbuf_write_no_spinlock)(int32_t input, RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(ringbuf_read_array_no_spinlock)(int32_t *output, uint32_t size, RINGBUFFER *ringbuffer);
extern int64_t __not_in_flash_func(ringbuf_write_array_no_spinlock)(int32_t *input, uint32_t size, RINGBUFFER *ringbuffer);
#endif