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

// for ES9010K2M
void ess_dac_i2c_setup(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if(KIND_ESS_DAC == ES9010K2M)
	{
		if(!CORE0_UPSAMPLING_192K)
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
		i2cbuf[1] = 0x00; // 100MHz
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

		if(!CORE0_UPSAMPLING_192K)
		{
			// 内蔵アップサンプリングを使用しない
			//i2cbuf[0] = 0x07; // Resister #7
			//i2cbuf[1] = 0x08; // bypass OSF
			//i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			//sleep_ms(1);

			// 128fsモードにする
			i2cbuf[0] = 0x0A; // Resister #10
			i2cbuf[1] = 0x12; // enable 128fs-mode, lock_speed=5461FSL edges(default)
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// DPLLバンド幅設定(ジッタが多いので少し広めに)
		i2cbuf[0] = 0x0C; // Resister 12
		i2cbuf[1] = 0xFA; // I2S:0xF, DSD:0xA(default)
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, true);
		sleep_ms(1);		
	}

	else if(KIND_ESS_DAC == ES9039Q2M)
	{
		if((!BYPASS_CORE1_UPSAMPLING) && (!CORE0_UPSAMPLING_192K))
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

		if(!CORE0_UPSAMPLING_192K)
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

void ess_dac_mute(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if(KIND_ESS_DAC == ES9038Q2M && ENABLE_ES9038Q2M_DEPOP)
	{
		i2cbuf[0] = 0x07; // Resister #7
		i2cbuf[1] = 0x01; // mute
		i2c_write_dma(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, false);
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
		i2c_write_dma(I2C_PORT, I2C_ESS_DAC_ADDR >>1, i2cbuf, 2, false);
	}
	is_ess_dac_mute = false;
}