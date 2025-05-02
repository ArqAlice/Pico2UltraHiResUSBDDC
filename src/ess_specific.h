/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _ESS_SPECIFIC_H_
#define _ESS_SPECIFIC_H_

#include "stdbool.h"

// ESS DAC Kind
#define ESS_DAC_NONE 0
#define ES9010K2M 1
#define ES9038Q2M 2
#define ES9039Q2M 3

// ESS DAC Default ADDR
#define ADDR0_NONE      0x00
#define ADDR0_ES9010K2M 0x90
#define ADDR0_ES9038Q2M 0x90
#define ADDR0_ES9039Q2M 0x90

extern void ess_dac_i2c_setup(void);
extern void ess_dac_initialize(void);
extern void ess_dac_activate(void);

extern bool get_ess_dac_mute(void);
extern void ess_dac_mute(void);
extern void ess_dac_unmute(void);

#endif