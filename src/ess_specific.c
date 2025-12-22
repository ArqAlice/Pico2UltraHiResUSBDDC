/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "ess_specific.h"
#include "hardware/i2c.h"
#include "common.h"
#include "stdbool.h"
#include "nonblocking_i2c.h"

static bool is_ess_dac_mute = false;
extern I2C_RINGBUFFER i2c_ringbuffer0;

void ess_dac_i2c_setup(void)
{
	uint8_t i2cbuf[2] = {0, 0};
	uint16_t ratio_core0 = get_ratio_upsampling_core0(audio_state.freq);
	uint16_t ratio_core1 = get_ratio_upsampling_core1();
	uint32_t output_fs = audio_state.freq * ratio_core0 * ratio_core1;
	bool external_upsampling = (ratio_core0 * ratio_core1) > 1;

	if(KIND_ESS_DAC == ES9010K2M)
	{
		if(external_upsampling)
		{
		// 内蔵アップサンプリングを使用しない
		i2cbuf[0] = 0x15; // Resister 21
		i2cbuf[1] = 0x01; // bypass OSF
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);
		}

		// DPLL/ASRCバンド幅設定(ジッタが多いので少し広めに)
		i2cbuf[0] = 0x0C; // Resister 12
		i2cbuf[1] = 0xC8; // default 0x5A, 0xB0~C8くらいから動く
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);
	}

	else if(KIND_ESS_DAC == ES9038Q2M)
	{
		// MCLK設定
		i2cbuf[0] = 0x00; // Resister #0 System Resisters
		i2cbuf[1] = 0x00; // 1/1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// ソフトスタート設定
		i2cbuf[0] = 0x0E; // Resister #14 Soft Start Configuration
		i2cbuf[1] = 0x8A; // 
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// volume1設定
		i2cbuf[0] = 0x0F; // Resister #15 Volume Control
		i2cbuf[1] = 0x00; // 
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// volume2設定
		i2cbuf[0] = 0x10; // Resister #16 Volume Control
		i2cbuf[1] = 0x00; // 
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// volume左右共通化
		//i2cbuf[0] = 0x1B; // Resister #27 General Configuration
		//i2cbuf[1] = 0xDC; // ch2 volume is shared with ch1
		//i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		//sleep_ms(1);

		// THD compensation
		if (ENABLE_ESS_DAC_THD_COMPEN)
		{
			i2cbuf[0] = 0x0D; // Resister #13 THD Bypass
			i2cbuf[1] = 0x00; // THD compensation enable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
			
			i2cbuf[0] = 0x16; // Resister #22 THD Compensation C2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			i2cbuf[0] = 0x17; // Resister #23 THD Compensation C2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x18; // Resister #24 THD Compensation C3
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			i2cbuf[0] = 0x19; // Resister #25 THD Compensation C3
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
			
		}
		else
		{
			i2cbuf[0] = 0x0D; // Resister #13 THD Bypass
			i2cbuf[1] = 0x40; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		}

		// DPLLバンド幅設定
		uint8_t bandwidth = ((uint8_t)ESS_DPLL_BANDWIDTH) >>4;
		i2cbuf[0] = 0x0C; // Resister 12
		i2cbuf[1] = ((bandwidth & 0xF) << 4) | 0x0A;
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// PLL LOCK SPEED
		i2cbuf[0] = 0x0A; // Resister 10
		i2cbuf[1] = 0x00 | ((uint8_t)ESS_DPLL_LOCKSPEED & 0xF);
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);
	}

	else if(KIND_ESS_DAC == ES9039Q2M)
	{
		if(output_fs >= 700000)
		{
			// 768kHz入力を有効化し、DACを有効化する
			i2cbuf[0] = 0x00; // Resister 0: SYSTEM_CONFIG
			i2cbuf[1] = 0x42; // Enable 64fs mode, enable DAC
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		else
		{
			// DACを有効化する
			i2cbuf[0] = 0x00; // Resister 0: SYSTEM_CONFIG
			i2cbuf[1] = 0x02; // enable DAC
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		if(external_upsampling)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x5A; // Resister 90: DAC_PATH_CONFIG
			i2cbuf[1] = 0x03; // bypass FIR2x, FIR4x
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// DAC interpolation path clock diable
		i2cbuf[0] = 0x01; // Resister 1: SYS_MODE_CONFIG
		i2cbuf[1] = 0xC1; // enable DAC_CLK, enable SYNC Mode, enable TDM decode
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);

		// DPLLバンド幅設定
		i2cbuf[0] = 0x1D; // Resister 29: DPLL_BW
		i2cbuf[1] = 0x30;
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);
	}
}

void ess_dac_initialize(void)
{
	gpio_init(DAC_ENABLE_PIN);
	gpio_set_dir(DAC_ENABLE_PIN, true);
	gpio_put(DAC_ENABLE_PIN, false); // Deactivate DAC
}

void ess_dac_activate(void)
{
	gpio_put(DAC_ENABLE_PIN, true);
}


bool get_ess_dac_mute(void)
{
	return is_ess_dac_mute;
}

void ess_dac_volume(void)
{
	static uint16_t volume = 0;
	if(audio_state.acq_volume != volume)
	{
		uint8_t i2cbuf[4] = {0, 0, 0, 0};
		if(KIND_ESS_DAC == ES9038Q2M)
		{
			float vol_dB_2 = -saturation_f32((float)audio_state.acq_volume / 128.0, 0.0, -256.0);
			i2cbuf[0] = 0x0F; // Resister #15 volume1
			i2cbuf[1] = (uint8_t)vol_dB_2;
			i2cbuf[2] = 0x10; // Resister #16 volume2
			i2cbuf[3] = (uint8_t)vol_dB_2;
		}
		else if(KIND_ESS_DAC == ES9039Q2M)
		{
			// TODO:作る
		}
		i2c_ringbuf_write_array(i2cbuf, 4, &i2c_ringbuffer0);
	}
	volume = audio_state.acq_volume;
}

void ess_dac_mute(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if(KIND_ESS_DAC == ES9038Q2M && ENABLE_ES9038Q2M_DEPOP)
	{
		i2cbuf[0] = 0x07; // Resister #7
		i2cbuf[1] = 0x01; // mute
		i2c_ringbuf_write_array(i2cbuf, 2, &i2c_ringbuffer0);
	}
	is_ess_dac_mute = true;
}

void ess_dac_unmute(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if(KIND_ESS_DAC == ES9038Q2M && ENABLE_ES9038Q2M_DEPOP)
	{
		i2cbuf[0] = 0x07; // Resister #7
		i2cbuf[1] = 0x00; // unmute
		i2c_ringbuf_write_array(i2cbuf, 2, &i2c_ringbuffer0);
	}
	is_ess_dac_mute = false;
}
