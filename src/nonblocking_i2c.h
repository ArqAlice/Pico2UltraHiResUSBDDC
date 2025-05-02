/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _NONBLOCKING_I2C_H_
#define _NONBLOCKING_I2C_H_

#include "hardware/i2c.h"

extern void i2c_dma_initialize(i2c_inst_t *i2c);
extern void i2c_write_dma(i2c_inst_t *i2c_inst, uint8_t addr_7bit, const uint8_t *data, size_t len, bool nostop);

#endif