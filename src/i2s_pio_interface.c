/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "i2s_pio_interface.h"
#include "common.h"

// PIO

// I2S_16bit_program → 16bitI2S（主にTDA1543等のNOS用）
// I2S_32bit_program → 32bitI2S（通常の32bitI2Sフォーマット）
// I2S_32bit_inv_program → 32bitI2S（32bitI2SのBCLKとLRCKのGPIOを反転したもの）
void I2S_16bit_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out)
{
	uint offset = pio_add_program(pio, &I2S_16bit_program);
	pio_sm_config sm_config = I2S_16bit_program_get_default_config(offset);

	pio_gpio_init(pio, data_pin);
	pio_gpio_init(pio, sideset_base);
	pio_gpio_init(pio, sideset_base + 1);

	pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);
	pio_sm_set_consecutive_pindirs(pio, sm, sideset_base, 2, true);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX); // RX FIFOを無効にして2倍のTX FIFOを使用する

	sm_config_set_out_pins(&sm_config, data_pin, 1);
	sm_config_set_sideset_pins(&sm_config, sideset_base);
	sm_config_set_out_shift(&sm_config, false, true, 32);
	float div = (float)clock_get_hz(clk_sys) / ((freq * get_ratio_upsampling_core0(freq) * get_ratio_upsampling_core1()) << 7u);
	sm_config_set_clkdiv(&sm_config, div);

	pio_sm_init(pio, sm, offset, &sm_config);
	pio_sm_set_enabled(pio, sm, true);

	*sm_config_out = sm_config;
	*offset_out = offset;
}

void I2S_32bit_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out)
{
	uint offset = pio_add_program(pio, &I2S_32bit_program);
	pio_sm_config sm_config = I2S_32bit_program_get_default_config(offset);

	pio_gpio_init(pio, data_pin);
	pio_gpio_init(pio, sideset_base);
	pio_gpio_init(pio, sideset_base + 1);

	pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);
	pio_sm_set_consecutive_pindirs(pio, sm, sideset_base, 2, true);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX); // RX FIFOを無効にして2倍のTX FIFOを使用する

	sm_config_set_out_pins(&sm_config, data_pin, 1);
	sm_config_set_sideset_pins(&sm_config, sideset_base);
	sm_config_set_out_shift(&sm_config, false, true, 32);
	float div = (float)clock_get_hz(clk_sys) / ((freq * get_ratio_upsampling_core0(freq) * get_ratio_upsampling_core1()) << 7u);
	sm_config_set_clkdiv(&sm_config, div);

	pio_sm_init(pio, sm, offset, &sm_config);
	pio_sm_set_enabled(pio, sm, true);

	*sm_config_out = sm_config;
	*offset_out = offset;
}

void I2S_32bit_inv_program_init(PIO pio, uint sm, uint data_pin, uint sideset_base, uint freq, pio_sm_config *sm_config_out, uint *offset_out)
{
	uint offset = pio_add_program(pio, &I2S_32bit_inv_program);
	pio_sm_config sm_config = I2S_32bit_program_get_default_config(offset);

	pio_gpio_init(pio, data_pin);
	pio_gpio_init(pio, sideset_base);
	pio_gpio_init(pio, sideset_base + 1);

	pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);
	pio_sm_set_consecutive_pindirs(pio, sm, sideset_base, 2, true);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX); // RX FIFOを無効にして2倍のTX FIFOを使用する

	sm_config_set_out_pins(&sm_config, data_pin, 1);
	sm_config_set_sideset_pins(&sm_config, sideset_base);
	sm_config_set_out_shift(&sm_config, false, true, 32);
	float div = (float)clock_get_hz(clk_sys) / ((freq * get_ratio_upsampling_core0(freq) * get_ratio_upsampling_core1()) << 7u);
	sm_config_set_clkdiv(&sm_config, div);

	pio_sm_init(pio, sm, offset, &sm_config);
	pio_sm_set_enabled(pio, sm, true);

	*sm_config_out = sm_config;
	*offset_out = offset;
}

// サンプルレート変更時に、PIOの分周率を再設定する
void I2S_freq_init(PIO pio, uint sm, pio_sm_config *sm_config, uint offset)
{
	pio_sm_set_enabled(pio, sm, false);

	float div = (float)clock_get_hz(clk_sys) / (float)((audio_state.freq * get_ratio_upsampling_core0(audio_state.freq) * get_ratio_upsampling_core1()) << 7u);
	sm_config_set_clkdiv(sm_config, div);
	pio_sm_init(pio, sm, offset, sm_config);
	pio_sm_set_enabled(pio, sm, true);
}