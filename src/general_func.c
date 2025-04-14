/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "common.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "transmit_to_dac.h"

extern inline int32_t saturation_i32(int32_t in, int32_t max, int32_t min)
{
	if (in > max)
		return max;
	else if (in < min)
		return min;
	return in;
}

extern inline float saturation_f32(float in, float max, float min)
{
	if (in > max)
		return max;
	else if (in < min)
		return min;
	return in;
}

// int32_t型をfloat型にまとめてキャスト
inline void int32_to_float_array(int32_t *input, float *output, uint32_t length)
{
    for(int i=0; i<length; i++)
        output[i] = (float)input[i];
}

// float型をint32_t型にまとめてキャスト
inline void float_to_int32_array(float *input, int32_t *output, uint32_t length)
{
    for(int i=0; i<length; i++)
        output[i] = (int32_t)input[i];
}

// アップサンプリング倍率取得関数
extern inline uint16_t get_ratio_upsampling_core0(uint32_t freq)
{
	uint16_t ratio;
	switch (freq)
	{
	case 192000:
	case 176400:
		ratio = RATIO_UPSAMPLING_192K;
		break;
	case 96000:
	case 88200:
		ratio = RATIO_UPSAMPLING_96K;
		break;
	case 48000:
	case 44100:
	default:
		ratio = RATIO_UPSAMPLING_48K;
		break;
	}

	if (!CORE0_UPSAMPLING_192K)
		return ratio;
	else
		return ratio >> 1;
}

inline uint16_t ratio_to_bitshift(uint16_t ratio)
{
	switch (ratio)
	{
	case 2:
		return 1;
	case 4:
		return 2;
	case 8:
		return 3;
	default:
		return 0;
	}
}

inline uint16_t get_ratio_upsampling_core1(void)
{
	if (is_high_power_mode && (!BYPASS_CORE1_UPSAMPLING) && (!CORE0_UPSAMPLING_192K))
	{
		return RATIO_UPSAMPLING_CORE1;
	}
	else if ((!BYPASS_CORE1_UPSAMPLING) && (!CORE0_UPSAMPLING_192K))
	{
		return RATIO_UPSAMPLING_CORE1 >> 1;
	}
	else // bypass Core1 upsampling
	{
		return 1;
	}
}

void renew_cpu_clock(bool is_high_power)
{
	// CPUクロックを再設定
	switch (audio_state.freq)
	{
	case 192000:
	case 96000:
	case 48000:
		if (is_high_power)
		{
			vreg_set_voltage(VREG_VOLTAGE_1_15);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_48K, true);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_48K, true);
			busy_wait_us(100);
			vreg_set_voltage(VREG_VOLTAGE_1_05);
		}
		break;
	case 176400:
	case 88200:
	case 44100:
	default:
		if (is_high_power)
		{
			vreg_set_voltage(VREG_VOLTAGE_1_15);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_44K, true);
			busy_wait_us(100);
			vreg_set_voltage(VREG_VOLTAGE_1_05);
		}
		break;
	}
}

void renew_clock(bool is_high_power)
{
	// Core0の割り込みタイマを停止
	cancel_timer0();

	// CPUクロックを再設定
	renew_cpu_clock(is_high_power);

	// Core0の割り込みタイマを再開
	restart_timer0();

	// DMAをクリア
	dma_stop_and_clear();

	// PIO分周率を再設定
	reset_i2s_freq();
}

// DACを設定するためのI2C
void setup_I2C(void)
{
	// running at 100kHz.
	i2c_init(I2C_PORT, 104 * 1000);
	gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
	gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(I2C_SDA);
	gpio_pull_up(I2C_SCL);
}

void volume_control(void)
{
	static float volume_slow = MIN_VOLUME;

	volume_slow += ((float)audio_state.acq_volume - volume_slow) * 0.85;

	audio_state.vol_float = saturation_f32(pow(10, volume_slow / VOLUME_RESOLUTION / 10.0), 1.0, 0);
}