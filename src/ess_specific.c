/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "ess_specific.h"
#include "hardware/i2c.h"
#include "common.h"

// for ES9010K2M
void ess_dac_i2c_setup(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if(KIND_ESS_DAC == ES9010K2M)
	{
		// DPLL/ASRCバンド幅設定(ジッタが多いので少し広めに)
		i2cbuf[0] = 0x0C;
		i2cbuf[1] = 0xC8; // default 0x5A, 0xB0~C8くらいから動く
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR, i2cbuf, 2, true);
		sleep_ms(10);

		// 内蔵アップサンプリングを使用しない
		i2cbuf[0] = 0x15;
		i2cbuf[1] = 0x01; // bypass OSF
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR, i2cbuf, 2, true);
		sleep_ms(10);
	}

	else if(KIND_ESS_DAC == ES9039Q2M)
	{
		if((!BYPASS_CORE1_UPSAMPLING) && (!CORE0_UPSAMPLING_192K))
		{
			// 768kHz入力を有効化する
			i2cbuf[0] = 0x00;
			i2cbuf[1] = 0x40; // Enable 64fs mode
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR, i2cbuf, 2, true);
			sleep_ms(10);
		}

		// 内蔵アップサンプリングを使用しない
		i2cbuf[0] = 0x5A;
		i2cbuf[1] = 0x03; // bypass FIR2x, FIR4x
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR, i2cbuf, 2, true);
		sleep_ms(10);

		// DPLLバンド幅設定(ジッタが多いので少し広めに)
		i2cbuf[0] = 0x1D;
		i2cbuf[1] = 0x80; // 
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR, i2cbuf, 2, true);
		sleep_ms(10);
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