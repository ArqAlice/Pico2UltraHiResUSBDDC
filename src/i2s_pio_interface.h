/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _I2S_PIO_INTERFACE_H_
#define _I2S_PIO_INTERFACE_H_

#include "hardware/pio.h"
#include "i2s.pio.h"
#include "hardware/clocks.h"

extern void I2S_16bit_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out);
extern void I2S_32bit_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out);
extern void I2S_32bit_inv_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out);
extern void I2S_freq_init(PIO pio, uint sm, pio_sm_config *sm_config, uint offset);

#endif /* _I2S_PIO_INTERFACE_H_ */