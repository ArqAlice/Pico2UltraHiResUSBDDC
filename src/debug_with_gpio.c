/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "debug_with_gpio.h"

static uint8_t gpio_assignment[4] = {0};
static volatile uint32_t offtime = 10;

extern void gpio_out_set_offtime(uint32_t offtime_)
{
    offtime = offtime_;
}

extern void initialize_gpio_debugging(uint8_t gpio0, uint8_t gpio1, uint8_t gpio2, uint8_t gpio3)
{
    gpio_assignment[0] = gpio0;
    gpio_assignment[1] = gpio1;
    gpio_assignment[2] = gpio2;
    gpio_assignment[3] = gpio3;

    for (uint8_t i = 0; i < 4; i++)
    {
        gpio_init(gpio_assignment[i]);
        gpio_set_dir(gpio_assignment[i], true);
    }
}

extern inline void gpio_toggle(uint8_t gpio)
{
    if (gpio_get(gpio) == false)
        gpio_put(2, true);
    else
        (gpio_put(gpio, false));
}

static void gpio_offtime(void)
{
    volatile uint32_t offtime_buffer = offtime;
    while (offtime_buffer--)
        tight_loop_contents();
}

extern inline void uint8_to_single_gpio(uint8_t gpio, uint8_t in_value)
{
    uint8_t in_buf = in_value;

    gpio_put(gpio, false);
    gpio_offtime();
    while (in_buf)
    {
        gpio_put(gpio, true);
        gpio_offtime();
        gpio_put(gpio, false);
        gpio_offtime();
        in_buf--;
    }
    sleep_us(1);
}

extern inline void uint16_to_gpio(int16_t in_value)
{
    value2gpio outvalue;
    outvalue.vUINT16_T = in_value;

    gpio_put(gpio_assignment[0], false);
    gpio_put(gpio_assignment[1], false);
    gpio_put(gpio_assignment[2], false);
    gpio_put(gpio_assignment[3], false);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit0);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit1);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit2);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit3);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit4);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit5);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit6);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit7);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit8);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit9);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit10);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit11);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit12);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit13);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit14);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit15);
    gpio_offtime();
}

extern inline void uint8_to_gpio(uint8_t in_value)
{
    value2gpio outvalue;
    outvalue.vUINT16_T = in_value & 0xFF;

    gpio_put(gpio_assignment[0], false);
    gpio_put(gpio_assignment[1], false);
    gpio_put(gpio_assignment[2], false);
    gpio_put(gpio_assignment[3], false);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit0);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit1);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit2);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit3);
    gpio_offtime();
    gpio_put(gpio_assignment[0], outvalue.gpio.bit4);
    gpio_put(gpio_assignment[1], outvalue.gpio.bit5);
    gpio_put(gpio_assignment[2], outvalue.gpio.bit6);
    gpio_put(gpio_assignment[3], outvalue.gpio.bit7);
    gpio_offtime();
}