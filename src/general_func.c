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
#include "nonblocking_i2c.h"

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
			vreg_set_voltage(V_CORE_HI);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_48K, true);
			busy_wait_us(50);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_48K, true);
			busy_wait_us(100);
			vreg_set_voltage(V_CORE_LO);
			busy_wait_us(50);
		}
		break;
	case 176400:
	case 88200:
	case 44100:
	default:
		if (is_high_power)
		{
			vreg_set_voltage(V_CORE_HI);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
			busy_wait_us(50);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_44K, true);
			busy_wait_us(100);
			vreg_set_voltage(V_CORE_LO);
			busy_wait_us(50);
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
	// run at 100kHz.
	i2c_init(I2C_PORT, 104 * 1000);
	gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
	gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(I2C_SDA);
	gpio_pull_up(I2C_SCL);

	i2c_dma_initialize(I2C_PORT);
}

void volume_control(void)
{
	if (!audio_state.mute)
	{
		if (ENABLE_ESS_DAC_VOLUME)
		{
			audio_state.vol_float = 1.0;
			ess_dac_volume();
		}
		else
		{
			audio_state.vol_float = saturation_f32(pow(10, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0), 1.0, 0);
		}
	}
	else
	{
		audio_state.vol_float = 0.;
	}

}