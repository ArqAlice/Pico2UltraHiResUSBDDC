/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "ringbuffer.h"

extern int16_t initialize_ringbuffer(uint32_t size, bool no_spinlock, RINGBUFFER *ringbuffer)
{
    ringbuffer->no_spinlock = no_spinlock;
    if (!no_spinlock)
        ringbuffer->spinlock = spin_lock_init(spin_lock_claim_unused(true));

    ringbuffer->size_buffer = size;
    ringbuffer->write_point = 0;
    ringbuffer->read_point = 0;
    ringbuffer->size_using = 0;
    ringbuffer->buffer = (int32_t *)malloc(sizeof(int32_t) * size);
    return 0;
}

extern void clear_ringbuffer(RINGBUFFER *ringbuffer)
{
    ringbuffer->write_point = 0;
    ringbuffer->read_point = 0;
    ringbuffer->size_using = 0;
}

extern bool __not_in_flash_func(ringbuffer_is_full)(RINGBUFFER *ringbuffer)
{
    if (ringbuffer->write_point - ringbuffer->read_point >= ringbuffer->size_buffer)
        return true;
    else
        return false;
}

extern int64_t __not_in_flash_func(get_size_using)(RINGBUFFER *ringbuffer)
{
    return ringbuffer->size_using;
}

extern int64_t __not_in_flash_func(get_size_remain)(RINGBUFFER *ringbuffer)
{
    return ringbuffer->size_buffer - ringbuffer->size_using;
}

extern uint32_t __not_in_flash_func(get_read_point)(RINGBUFFER *ringbuffer)
{
    return ringbuffer->read_point;
}

extern uint32_t __not_in_flash_func(get_write_point)(RINGBUFFER *ringbuffer)
{
    return ringbuffer->write_point;
}

extern int16_t __not_in_flash_func(ringbuf_write_spinlock)(int32_t input, RINGBUFFER *ringbuffer)
{
    volatile uint32_t owner; // for spinlock

    if (ringbuffer->size_using == ringbuffer->size_buffer)
    {
        return -1; // buffer is full
    }

    *(ringbuffer->buffer + (ringbuffer->write_point)) = input;
    owner = spin_lock_blocking(ringbuffer->spinlock);
    ringbuffer->write_point++;
    if (ringbuffer->write_point >= ringbuffer->size_buffer)
        ringbuffer->write_point = 0;
    ringbuffer->size_using++;
    spin_unlock(ringbuffer->spinlock, owner);
    return 1;
}

extern int16_t __not_in_flash_func(ringbuf_write_no_spinlock)(int32_t input, RINGBUFFER *ringbuffer)
{
    if (ringbuffer->size_using == ringbuffer->size_buffer)
    {
        return -1; // buffer is full
    }

    *(ringbuffer->buffer + (ringbuffer->write_point)) = input;
    ringbuffer->write_point++;
    if (ringbuffer->write_point >= ringbuffer->size_buffer)
        ringbuffer->write_point = 0;
    ringbuffer->size_using++;
    return 1;
}

extern int16_t __not_in_flash_func(ringbuf_read_spinlock)(int32_t *output, RINGBUFFER *ringbuffer)
{
    volatile uint32_t owner; // for spinlock
    if (ringbuffer->size_using == 0)
    {
        return -1; // buffer is empty
    }

    *output = *(ringbuffer->buffer + (ringbuffer->read_point));

    owner = spin_lock_blocking(ringbuffer->spinlock);
    ringbuffer->read_point++;
    if (ringbuffer->read_point >= ringbuffer->size_buffer)
        ringbuffer->read_point = 0;
    ringbuffer->size_using--;
    spin_unlock(ringbuffer->spinlock, owner);
    return 1;
}

extern int16_t __not_in_flash_func(ringbuf_read_no_spinlock)(int32_t *output, RINGBUFFER *ringbuffer)
{
    if (ringbuffer->size_using == 0)
    {
        return -1; // buffer is empty
    }

    *output = *(ringbuffer->buffer + (ringbuffer->read_point));

    ringbuffer->read_point++;
    if (ringbuffer->read_point >= ringbuffer->size_buffer)
        ringbuffer->read_point = 0;
    ringbuffer->size_using--;
    return 1;
}

extern int64_t __not_in_flash_func(ringbuf_read_array_spinlock)(int32_t *output, uint32_t size, RINGBUFFER *ringbuffer)
{
    volatile uint32_t owner; // for spinlock

    if (ringbuffer->size_using == 0 || ringbuffer->size_using < size)
    {
        return -1; // buffer is empty, or now buffer usage is not bigger than requested size
    }

    owner = spin_lock_blocking(ringbuffer->spinlock);

    int32_t rx1_size = ((ringbuffer->read_point + size) > ringbuffer->size_buffer) ? ringbuffer->size_buffer - ringbuffer->read_point : size;
    int32_t rx2_size = size - rx1_size;

    memcpy(output, ringbuffer->buffer + ringbuffer->read_point, sizeof(int32_t) * rx1_size);
    if (rx2_size > 0)
        memcpy(output + rx1_size, ringbuffer->buffer, sizeof(int32_t) * rx2_size);

    int32_t inner_read_point = ringbuffer->read_point + size;
    if (inner_read_point > ringbuffer->size_buffer)
        inner_read_point -= ringbuffer->size_buffer;
    ringbuffer->read_point = inner_read_point;

    ringbuffer->size_using -= size;

    spin_unlock(ringbuffer->spinlock, owner);
    return size;
}

extern int64_t __not_in_flash_func(ringbuf_read_array_no_spinlock)(int32_t *output, uint32_t size, RINGBUFFER *ringbuffer)
{
    if (ringbuffer->size_using == 0 || ringbuffer->size_using < size)
    {
        return -1; // buffer is empty, or now buffer usage is not bigger than requested size
    }

    int32_t rx1_size = ((ringbuffer->read_point + size) > ringbuffer->size_buffer) ? ringbuffer->size_buffer - ringbuffer->read_point : size;
    int32_t rx2_size = size - rx1_size;

    memcpy(output, ringbuffer->buffer + ringbuffer->read_point, sizeof(int32_t) * rx1_size);
    if (rx2_size > 0)
        memcpy(output + rx1_size, ringbuffer->buffer, sizeof(int32_t) * rx2_size);

    int32_t inner_read_point = ringbuffer->read_point + size;
    if (inner_read_point > ringbuffer->size_buffer)
        inner_read_point -= ringbuffer->size_buffer;
    ringbuffer->read_point = inner_read_point;

    ringbuffer->size_using -= size;

    return size;
}

extern int64_t __not_in_flash_func(ringbuf_write_array_spinlock)(int32_t *input, uint32_t size, RINGBUFFER *ringbuffer)
{
    volatile uint32_t owner; // for spinlock
    volatile int64_t remain_size = ringbuffer->size_buffer - ringbuffer->size_using;

    if (ringbuffer->size_using == ringbuffer->size_buffer || remain_size < size)
        return -1; // buffer is full

    if (size <= 0)
        return 0;

    owner = spin_lock_blocking(ringbuffer->spinlock);

    int32_t tx1_size = ((ringbuffer->write_point + size) > ringbuffer->size_buffer) ? ringbuffer->size_buffer - ringbuffer->write_point : size;
    int32_t tx2_size = size - tx1_size;

    memcpy(ringbuffer->buffer + ringbuffer->write_point, input, sizeof(int32_t) * tx1_size);
    if (tx2_size > 0)
        memcpy(ringbuffer->buffer, input + tx1_size, sizeof(int32_t) * tx2_size);

    int32_t inner_write_point = ringbuffer->write_point + size;
    if (inner_write_point > ringbuffer->size_buffer)
        inner_write_point -= ringbuffer->size_buffer;
    ringbuffer->write_point = inner_write_point;

    ringbuffer->size_using += size;

    spin_unlock(ringbuffer->spinlock, owner);
    return size;
}

extern int64_t __not_in_flash_func(ringbuf_write_array_no_spinlock)(int32_t *input, uint32_t size, RINGBUFFER *ringbuffer)
{
    volatile int64_t remain_size = ringbuffer->size_buffer - ringbuffer->size_using;

    if (ringbuffer->size_using == ringbuffer->size_buffer || remain_size < size)
    {
        return -1; // buffer is full
    }

    int32_t tx1_size = ((ringbuffer->write_point + size) > ringbuffer->size_buffer) ? ringbuffer->size_buffer - ringbuffer->write_point : size;
    int32_t tx2_size = size - tx1_size;

    memcpy(ringbuffer->buffer + ringbuffer->write_point, input, sizeof(int32_t) * tx1_size);
    if (tx2_size > 0)
        memcpy(ringbuffer->buffer, input + tx1_size, sizeof(int32_t) * tx2_size);

    int32_t inner_write_point = ringbuffer->write_point + size;
    if (inner_write_point > ringbuffer->size_buffer)
        inner_write_point -= ringbuffer->size_buffer;
    ringbuffer->write_point = inner_write_point;

    ringbuffer->size_using += size;

    return size;
}