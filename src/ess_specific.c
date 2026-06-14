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

	if (KIND_ESS_DAC == ES9010K2M)
	{
		if (output_fs > 300000)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x15; // Resister 21
			i2cbuf[1] = 0x01; // bypass OSF
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// DPLL/ASRCバンド幅設定(ジッタが多いので少し広めに)
		i2cbuf[0] = 0x0C; // Resister 12
		i2cbuf[1] = 0xC8; // default 0x5A, 0xB0~C8くらいから動く
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);
	}

	else if (KIND_ESS_DAC == ES9038Q2M)
	{
		// MCLK設定
		i2cbuf[0] = 0x00; // Resister #0 System Resisters
		i2cbuf[1] = 0x00; // 1/1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// ソフトスタート設定
		i2cbuf[0] = 0x0E; // Resister #14 Soft Start Configuration
		i2cbuf[1] = 0x8A; //
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// THD compensation
		if (ENABLE_ESS_DAC_THD_COMPEN)
		{
			i2cbuf[0] = 0x0D; // Resister #13 THD Bypass
			i2cbuf[1] = 0x00; // THD compensation enable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x16; // Resister #22 THD Compensation C2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x17; // Resister #23 THD Compensation C2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x18; // Resister #24 THD Compensation C3
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x19; // Resister #25 THD Compensation C3
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		else
		{
			i2cbuf[0] = 0x0D; // Resister #13 THD Bypass
			i2cbuf[1] = 0x40; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		}

		// DPLLバンド幅設定
		uint8_t bandwidth = ((uint8_t)ESS_DPLL_BANDWIDTH) >> 4;
		i2cbuf[0] = 0x0C; // Resister 12
		i2cbuf[1] = ((bandwidth & 0xF) << 4) | 0x0A;
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// PLL LOCK SPEED
		i2cbuf[0] = 0x0A; // Resister 10
		i2cbuf[1] = 0x00 | ((uint8_t)ESS_DPLL_LOCKSPEED & 0xF);
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// ボリュームを最大にしておく
		i2cbuf[0] = 0x0F; // Resister #15 Volume Control ch1
		i2cbuf[1] = 0x00; // 最大ボリューム
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x10; // Resister #16 Volume Control ch2
		i2cbuf[1] = 0x00; // 最大ボリューム
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);
	}

	else if (KIND_ESS_DAC == ES9039Q2M)
	{
		// CLK GEAR SELECT
		i2cbuf[0] = 0x05; // Resister 5: CLK GEAR SELECT
		i2cbuf[1] = 0x00; // SYS_CLK=MCLK, AUTO_CLK_GEAR disabled
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// 768kHz入力を有効化し、DACを有効化する
		i2cbuf[0] = 0x00; // Resister 0: SYSTEM_CONFIG
		i2cbuf[1] = 0x42; // Enable 64fs mode, enable DAC
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// SYS MODE CONFIG
		i2cbuf[0] = 0x01; // Resister 1: SYS_MODE_CONFIG
		i2cbuf[1] = 0xB1; // enable DAC_CLK, disable Sync Mode, enable TDM decode
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// DAC CLOCK CONFIG
		i2cbuf[0] = 0x03; // Resister 3: DAC_CLOCK_CONFIG
		i2cbuf[1] = 0x00; // disable AutoFS Detect
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		if (output_fs > 300000)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x5A; // Resister 90: DAC_PATH_CONFIG
			i2cbuf[1] = 0x03; // bypass FIR2x, FIR4x
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		else if (output_fs > 700000)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x5A; // Resister 90: DAC_PATH_CONFIG
			i2cbuf[1] = 0x07; // bypass FIR2x, FIR4x, IIR
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// THD compensation
		if (ENABLE_ESS_DAC_THD_COMPEN)
		{
			i2cbuf[0] = 0x5B; // Resister #91 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5C; // Resister #92 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);

			i2cbuf[0] = 0x5D; // Resister #93 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5E; // Resister #94 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x6B; // Resister #107 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6C; // Resister #108 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);

			i2cbuf[0] = 0x6D; // Resister #109 THD Compensation C3 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6E; // Resister #110 THD Compensation C3 CH2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		else
		{
			i2cbuf[0] = 0x0D; // Resister #13 THD Bypass
			i2cbuf[1] = 0x40; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		}

		// DPLLバンド幅設定
		uint8_t bandwidth = ((uint8_t)ESS_DPLL_BANDWIDTH);
		i2cbuf[0] = 0x1D; // Resister 29: DPLL_BW
		i2cbuf[1] = bandwidth;
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);
	}
	else if (KIND_ESS_DAC == ES9039PRO)
	{
		// 768kHz入力を有効化し、DACを有効化する
		i2cbuf[0] = 0x00; // Resister 0: SYSTEM_CONFIG
		i2cbuf[1] = 0x42; // Enable 64fs mode, enable DAC
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// SYS MODE CONFIG
		i2cbuf[0] = 0x01; // Resister 1: SYS_MODE_CONFIG
		i2cbuf[1] = 0xB1; // enable DAC_CLK, disable Sync Mode, enable TDM decode
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// DAC CLOCK CONFIG
		i2cbuf[0] = 0x03; // Resister 3: DAC_CLOCK_CONFIG
		i2cbuf[1] = 0x00; // disable AutoFS Detect
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// CLK GEAR SELECT
		i2cbuf[0] = 0x05; // Resister 5: CLK GEAR SELECT
		i2cbuf[1] = 0x00; // SYS_CLK=MCLK, AUTO_CLK_GEAR disabled
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		if (output_fs > 300000)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x5A; // Resister 90: DAC_PATH_CONFIG
			i2cbuf[1] = 0x03; // bypass FIR2x, FIR4x
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		else if (output_fs > 700000)
		{
			// 内蔵アップサンプリングを使用しない
			i2cbuf[0] = 0x5A; // Resister 90: DAC_PATH_CONFIG
			i2cbuf[1] = 0x07; // bypass FIR2x, FIR4x, IIR
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// チャネル割り当て
		i2cbuf[0] = 0x40; // Resister 64: TDM_CH1_CONFIG
		i2cbuf[1] = 0x00; // TDM CH1 enable, TDM_CH1 slot0
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x41; // Resister 65: TDM_CH2_CONFIG
		i2cbuf[1] = 0x01; // TDM CH2 enable, TDM_CH2 slot1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x42; // Resister 66: TDM_CH3_CONFIG
		i2cbuf[1] = 0x00; // TDM CH3 enable, TDM_CH3 slot0
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x43; // Resister 67: TDM_CH4_CONFIG
		i2cbuf[1] = 0x01; // TDM CH4 enable, TDM_CH4 slot1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x44; // Resister 68: TDM_CH5_CONFIG
		i2cbuf[1] = 0x00; // TDM CH5 enable, TDM_CH5 slot0
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x45; // Resister 69: TDM_CH6_CONFIG
		i2cbuf[1] = 0x01; // TDM CH6 enable, TDM_CH6 slot1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x46; // Resister 70: TDM_CH7_CONFIG
		i2cbuf[1] = 0x00; // TDM CH7 enable, TDM_CH7 slot0
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		i2cbuf[0] = 0x47; // Resister 71: TDM_CH8_CONFIG
		i2cbuf[1] = 0x01; // TDM CH8 enable, TDM_CH8 slot1
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
		sleep_ms(1);

		// THD compensation
		if (ENABLE_ESS_DAC_THD_COMPEN)
		{
			i2cbuf[0] = 0x5B; // Resister #91 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5C; // Resister #92 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5D; // Resister #93 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5E; // Resister #94 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5F; // Resister #95 THD Compensation C2 CH3
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x60; // Resister #96 THD Compensation C2 CH3
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x61; // Resister #97 THD Compensation C2 CH4
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x62; // Resister #98 THD Compensation C2 CH4
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x63; // Resister #99 THD Compensation C2 CH5
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x64; // Resister #100 THD Compensation C2 CH5
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x65; // Resister #101 THD Compensation C2 CH6
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x66; // Resister #102 THD Compensation C2 CH6
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x67; // Resister #103 THD Compensation C2 CH7
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x68; // Resister #104 THD Compensation C2 CH7
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x69; // Resister #105 THD Compensation C2 CH8
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6A; // Resister #106 THD Compensation C2 CH8
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x6B; // Resister #107 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6C; // Resister #108 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6D; // Resister #109 THD Compensation C3 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6E; // Resister #110 THD Compensation C3 CH2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6F; // Resister #111 THD Compensation C3 CH3
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x70; // Resister #112 THD Compensation C3 CH3
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x71; // Resister #113 THD Compensation C3 CH4
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x72; // Resister #114 THD Compensation C3 CH4
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x73; // Resister #115 THD Compensation C3 CH5
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x74; // Resister #116 THD Compensation C3 CH5
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x75; // Resister #117 THD Compensation C3 CH6
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x76; // Resister #118 THD Compensation C3 CH6
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x77; // Resister #119 THD Compensation C3 CH7
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x78; // Resister #120 THD Compensation C3 CH7
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x79; // Resister #121 THD Compensation C3 CH8
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x7A; // Resister #122 THD Compensation C3 CH8
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}
		// THD補償無効
		else
		{
			i2cbuf[0] = 0x5B; // Resister #91 THD Compensation C2 CH1
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5C; // Resister #92 THD Compensation C2 CH1
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5D; // Resister #93 THD Compensation C2 CH2
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5E; // Resister #94 THD Compensation C2 CH2
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5F; // Resister #95 THD Compensation C2 CH3
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x60; // Resister #96 THD Compensation C2 CH3
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x61; // Resister #97 THD Compensation C2 CH4
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x62; // Resister #98 THD Compensation C2 CH4
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x63; // Resister #99 THD Compensation C2 CH5
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x64; // Resister #100 THD Compensation C2 CH5
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x65; // Resister #101 THD Compensation C2 CH6
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x66; // Resister #102 THD Compensation C2 CH6
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x67; // Resister #103 THD Compensation C2 CH7
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x68; // Resister #104 THD Compensation C2 CH7
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x69; // Resister #105 THD Compensation C2 CH8
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6A; // Resister #106 THD Compensation C2 CH8
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x6B; // Resister #107 THD Compensation C3 CH1
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6C; // Resister #108 THD Compensation C3 CH1
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6D; // Resister #109 THD Compensation C3 CH2
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6E; // Resister #110 THD Compensation C3 CH2
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6F; // Resister #111 THD Compensation C3 CH3
			i2cbuf[1] = 0x00; // THD compensation disable	
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x70; // Resister #112 THD Compensation C3 CH3
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x71; // Resister #113 THD Compensation C3 CH4
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x72; // Resister #114 THD Compensation C3 CH4
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x73; // Resister #115 THD Compensation C3 CH5
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x74; // Resister #116 THD Compensation C3 CH5
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x75; // Resister #117 THD Compensation C3 CH6
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x76; // Resister #118 THD Compensation C3 CH6
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x77; // Resister #119 THD Compensation C3 CH7
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x78; // Resister #120 THD Compensation C3 CH7
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x79; // Resister #121 THD Compensation C3 CH8
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x7A; // Resister #122 THD Compensation C3 CH8
			i2cbuf[1] = 0x00; // THD compensation disable
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);
		}

		// DPLLバンド幅設定
		uint8_t bandwidth = ((uint8_t)ESS_DPLL_BANDWIDTH);
		i2cbuf[0] = 0x1D; // Resister 29: DPLL_BW
		i2cbuf[1] = bandwidth & 0xF0; // DPLL bandwidth setting (upper 4 bits)
		i2cbuf[1] |= 0x1; // Async64fs mode enable
		i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
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
	static int16_t volume = 0;
	if (audio_state.acq_volume != volume)
	{
		uint8_t i2cbuf[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		I2C_RB_DATA i2c_rb_buf;

		if (KIND_ESS_DAC == ES9038Q2M)
		{
			float vol_dB_2 = -saturation_f32((float)audio_state.acq_volume / 128.0, 0.0, -256.0);
			i2cbuf[0] = 0x0F; // Resister #15 volume1
			i2cbuf[1] = (uint8_t)vol_dB_2;
			i2cbuf[2] = (uint8_t)vol_dB_2;

			i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 3, false, &i2c_rb_buf);
			i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

			// THD compensationの音量による影響を補正する
			if (ENABLE_ESS_THD_COMPEN_VOL_CORR && ENABLE_ESS_DAC_THD_COMPEN)
			{
				float vol = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.001f);
				float log10_vol = log10f(vol * 10.0f);
				float compen_c2 = ((float)ESS_THD_COMPEN_C2) * log10_vol;
				float compen_c3 = ((float)ESS_THD_COMPEN_C3) * log10_vol;
				i2cbuf[0] = 0x16; // THD Compensation start address
				i2cbuf[1] = (uint8_t)(((int16_t)compen_c2) & 0xFF);
				i2cbuf[2] = (uint8_t)((((int16_t)compen_c2) & 0xFF00) >> 8);
				i2cbuf[3] = (uint8_t)(((int16_t)compen_c3) & 0xFF);
				i2cbuf[4] = (uint8_t)((((int16_t)compen_c3) & 0xFF00) >> 8);

				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 5, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);
			}
		}
		else if (KIND_ESS_DAC == ES9039Q2M)
		{
			float vol_dB_2 = -saturation_f32((float)audio_state.acq_volume / 128.0, 0.0, -256.0);
			i2cbuf[0] = 0x4A; // Resister #74 volume ch1
			i2cbuf[1] = (uint8_t)vol_dB_2;
			i2cbuf[2] = (uint8_t)vol_dB_2;

			i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 3, false, &i2c_rb_buf);
			i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

			// THD compensationの音量による影響を補正する
			if (ENABLE_ESS_THD_COMPEN_VOL_CORR && ENABLE_ESS_DAC_THD_COMPEN)
			{
				float vol = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.001f);
				float log10_vol = log10f(vol * 10.0f);
				float compen_c2 = ((float)ESS_THD_COMPEN_C2) * log10_vol;
				float compen_c3 = ((float)ESS_THD_COMPEN_C3) * log10_vol;
				uint8_t c2_l = (uint8_t)(((int16_t)compen_c2) & 0xFF);
				uint8_t c2_h = (uint8_t)((((int16_t)compen_c2) & 0xFF00) >> 8);
				uint8_t c3_l = (uint8_t)(((int16_t)compen_c3) & 0xFF);
				uint8_t c3_h = (uint8_t)((((int16_t)compen_c3) & 0xFF00) >> 8);
				i2cbuf[0] = 0x5B; // THD Compensation C2 start address
				i2cbuf[1] = c2_l;
				i2cbuf[2] = c2_h;
				i2cbuf[3] = c2_l;
				i2cbuf[4] = c2_h;

				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 5, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

				i2cbuf[0] = 0x6B; // THD Compensation C3 start address
				i2cbuf[1] = c3_l;
				i2cbuf[2] = c3_h;
				i2cbuf[3] = c3_l;
				i2cbuf[4] = c3_h;

				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 5, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);
			}
		}
		else if(KIND_ESS_DAC == ES9039PRO)
		{
			float vol_dB_2 = -saturation_f32((float)audio_state.acq_volume / 128.0, 0.0, -256.0);
			i2cbuf[0] = 0x4A; // Resister #74 volume ch1
			i2cbuf[1] = (uint8_t)vol_dB_2;
			i2cbuf[2] = (uint8_t)vol_dB_2;
			i2cbuf[3] = (uint8_t)vol_dB_2;
			i2cbuf[4] = (uint8_t)vol_dB_2;
			i2cbuf[5] = (uint8_t)vol_dB_2;
			i2cbuf[6] = (uint8_t)vol_dB_2;
			i2cbuf[7] = (uint8_t)vol_dB_2;
			i2cbuf[8] = (uint8_t)vol_dB_2;

			i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 9, false, &i2c_rb_buf);
			i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

			// THD compensationの音量による影響を補正する
			if (ENABLE_ESS_THD_COMPEN_VOL_CORR && ENABLE_ESS_DAC_THD_COMPEN)
			{
				float vol = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.001f);
				float log10_vol = log10f(vol * 10.0f);
				float compen_c2 = ((float)ESS_THD_COMPEN_C2) * log10_vol;
				float compen_c3 = ((float)ESS_THD_COMPEN_C3) * log10_vol;
				uint8_t c2_l = (uint8_t)(((int16_t)compen_c2) & 0xFF);
				uint8_t c2_h = (uint8_t)((((int16_t)compen_c2) & 0xFF00) >> 8);
				uint8_t c3_l = (uint8_t)(((int16_t)compen_c3) & 0xFF);
				uint8_t c3_h = (uint8_t)((((int16_t)compen_c3) & 0xFF00) >> 8);

				i2cbuf[0] = 0x5B; // THD Compensation C2 start address
				i2cbuf[1] = c2_l;
				i2cbuf[2] = c2_h;
				i2cbuf[3] = c2_l;
				i2cbuf[4] = c2_h;
				i2cbuf[5] = c2_l;
				i2cbuf[6] = c2_h;
				i2cbuf[7] = c2_l;
				i2cbuf[8] = c2_h;
				i2cbuf[9] = c2_l;
				i2cbuf[10] = c2_h;
				i2cbuf[11] = c2_l;
				i2cbuf[12] = c2_h;
				i2cbuf[13] = c2_l;
				i2cbuf[14] = c2_h;
				i2cbuf[15] = c2_l;
				i2cbuf[16] = c2_h;
				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 17, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

				i2cbuf[0] = 0x6B; // THD Compensation C3 start address
				i2cbuf[1] = c3_l;
				i2cbuf[2] = c3_h;
				i2cbuf[3] = c3_l;
				i2cbuf[4] = c3_h;
				i2cbuf[5] = c3_l;
				i2cbuf[6] = c3_h;
				i2cbuf[7] = c3_l;
				i2cbuf[8] = c3_h;
				i2cbuf[9] = c3_l;
				i2cbuf[10] = c3_h;
				i2cbuf[11] = c3_l;
				i2cbuf[12] = c3_h;
				i2cbuf[13] = c3_l;
				i2cbuf[14] = c3_h;
				i2cbuf[15] = c3_l;
				i2cbuf[16] = c3_h;
				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 17, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);
			}
		}
	}
	volume = audio_state.acq_volume;
}

void ess_dac_mute(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if (KIND_ESS_DAC == ES9038Q2M && ENABLE_ES9038Q2M_DEPOP)
	{
		i2cbuf[0] = 0x07; // Resister #7
		i2cbuf[1] = 0x01; // mute

		I2C_RB_DATA i2c_rb_buf;
		i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, false, &i2c_rb_buf);
		i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);
	}
	is_ess_dac_mute = true;
}

void ess_dac_unmute(void)
{
	uint8_t i2cbuf[2] = {0, 0};

	if (KIND_ESS_DAC == ES9038Q2M && ENABLE_ES9038Q2M_DEPOP)
	{
		i2cbuf[0] = 0x07; // Resister #7
		i2cbuf[1] = 0x00; // unmute

		I2C_RB_DATA i2c_rb_buf;
		i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, false, &i2c_rb_buf);
		i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);
	}
	is_ess_dac_mute = false;
}
