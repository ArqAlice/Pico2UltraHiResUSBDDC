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
#include "limits.h"

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
			i2cbuf[0] = 0x5B; // Resister #22 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5C; // Resister #23 THD Compensation C2 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x5D; // Resister #22 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C2 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x5E; // Resister #23 THD Compensation C2 CH2
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C2 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x6B; // Resister #24 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6C; // Resister #25 THD Compensation C3 CH1
			i2cbuf[1] = (uint8_t)((ESS_THD_COMPEN_C3 & 0xFF00) >> 8);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			sleep_ms(1);

			i2cbuf[0] = 0x6D; // Resister #24 THD Compensation C3 CH2
			i2cbuf[1] = (uint8_t)(ESS_THD_COMPEN_C3 & 0xFF);
			i2c_write_blocking(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 2, true);
			i2cbuf[0] = 0x6E; // Resister #25 THD Compensation C3 CH2
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

float __not_in_flash_func(get_ess_dac_modulator_segment_position)(float volume, float declip_gain, float amplitude, float corr_offset, uint16_t num_segment)
{
	float absolute_volume = fabsf(volume * amplitude / (float)INT32_MAX * declip_gain);

	float segment_index_float = absolute_volume / (float)num_segment;

	uint16_t segment_index = (uint16_t)segment_index_float;
	float segment_position = segment_index_float - (float)segment_index;

	return saturation_f32(segment_position + corr_offset, 1.0f, 0.0f);
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
				float vol = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.0000001f);
				// float corr = get_ess_dac_modulator_segment_position(vol, DEFAULT_GAIN_RATIO, 1.0, 0.2, ESS_DAC_NUM_MODULATOR_SEGMENTS);
				float compen_c2 = saturation_f32(((float)ESS_THD_COMPEN_C2) / vol, 32767.0f, -32768.0f);
				float compen_c3 = saturation_f32(((float)ESS_THD_COMPEN_C3) / vol, 32767.0f, -32768.0f);
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
				float vol = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.0000001f);
				// float corr = get_ess_dac_modulator_segment_position(vol, DEFAULT_GAIN_RATIO, 1.0, 0.2, ESS_DAC_NUM_MODULATOR_SEGMENTS);
				float compen_c2 = saturation_f32(((float)ESS_THD_COMPEN_C2) / vol, 32767.0f, -32768.0f);
				float compen_c3 = saturation_f32(((float)ESS_THD_COMPEN_C3) / vol, 32767.0f, -32768.0f);
				i2cbuf[0] = 0x5B; // THD Compensation C2 start address
				i2cbuf[1] = (uint8_t)(((int16_t)compen_c2) & 0xFF);
				i2cbuf[2] = (uint8_t)((((int16_t)compen_c2) & 0xFF00) >> 8);
				i2cbuf[3] = (uint8_t)(((int16_t)compen_c2) & 0xFF);
				i2cbuf[4] = (uint8_t)((((int16_t)compen_c2) & 0xFF00) >> 8);

				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 5, false, &i2c_rb_buf);
				i2c_ringbuf_write(&i2c_rb_buf, &i2c_ringbuffer0);

				i2cbuf[0] = 0x6B; // THD Compensation C3 start address
				i2cbuf[1] = (uint8_t)(((int16_t)compen_c3) & 0xFF);
				i2cbuf[2] = (uint8_t)((((int16_t)compen_c3) & 0xFF00) >> 8);
				i2cbuf[3] = (uint8_t)(((int16_t)compen_c3) & 0xFF);
				i2cbuf[4] = (uint8_t)((((int16_t)compen_c3) & 0xFF00) >> 8);

				i2c_ringbuf_set_data(I2C_PORT, I2C_ESS_DAC_ADDR >> 1, i2cbuf, 5, false, &i2c_rb_buf);
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
