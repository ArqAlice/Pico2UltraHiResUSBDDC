/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ringbuffer.h"
#include "ess_specific.h"

#define CORE1_FIR_MODE_HALF_BAND (0)
#define CORE1_FIR_MODE_POLYPHASE (1)
#define CORE1_FIR_MODE_FFT (2)

// User Configurable ------------------------------------------------------------------

// Test mode
#define TEST_MODE (true)
#define TEST_PIN1 (1)
#define TEST_PIN2 (2)

// String Desc.
#define MFG_NAME ("ArqAlice")
#define DEVICE_NAME ("Pico2 UltraHiRes USB-DDC")
#define WEBSITE_ADDR ("y.tomi0131@gmail.com:")

// Faster I2S slew rate
#define I2S_SLEWRATE_FAST_ENABLE (true)

// Enhancement I2S signal output current
#define I2S_STRENGTH_REINFORCE_ENABLE (true)

// Power Mode Switch Pin
// The Hi-Power Mode, Core0 uses 384KHz FIR Filter
#define POWER_MODE_SWITCH_PIN (0)
#define ALWAYS_HIGH_POWER (true)
#define ALWAYS_LOW_POWER (false)

// I2C
#define I2C_PORT (i2c1)
#define I2C_SDA (6)
#define I2C_SCL (7)

// I2S Pin : sideset0:BCLK, sideset1:LRCK (if No Changed)
#define I2S_SIDESET_CHANGE (false)
#define I2S_DATA_PIN (26)
#define I2S_SIDESET_BASE (27)

// Upsampler control
// Core0 ratio is for 48k family; 96k uses /2, 192k uses /4 (min 1).
#define CORE0_UP_RATIO_HP (8)
#define CORE0_UP_RATIO_LP (4)
// Core1 ratio is applied directly (1/2/4 are supported).
#define CORE1_UP_RATIO_HP (2)
#define CORE1_UP_RATIO_LP (1)
#define DEFAULT_GAIN_RATIO (0.75) // Adjust this according to your filter to avoid clipping.

// ESS DAC Specific
#define USE_ESS_DAC (true)
#define KIND_ESS_DAC (ES9038Q2M)
#define I2C_ESS_DAC_ADDR (ADDR0_ES9038Q2M)
#define ENABLE_ES9038Q2M_DEPOP (true)
#define ENABLE_ESS_DAC_VOLUME (true)
#define ENABLE_ESS_DAC_THD_COMPEN (true)
#define ESS_THD_COMPEN_C2 (3) // 16bit signed int
#define ESS_THD_COMPEN_C3 (-99) // 16bit signed int
#define ESS_DPLL_BANDWIDTH (0x80) // 0~255, 0 is DPLL off
#define ESS_DPLL_LOCKSPEED (8)   // 0~16
#define TIME_ES9038Q2M_DEPOP_USEC (40000)
#define DAC_ENABLE_PIN (5)

// Other Function
#define USE_EXT_POWER_ENABLE (false)
#define EXT_POWER_ENABLE_PIN (3)
#define BOOT_WAIT_TIME_US (50000)

// User Configurable end ------------------------------------------------------------

// システムクロック
//#define SYS_CLOCK_KHZ_44K (412000)
//#define SYS_CLOCK_KHZ_48K (430000)
#define SYS_CLOCK_KHZ_44K (282000)
#define SYS_CLOCK_KHZ_48K (307200)
#define SYS_CLOCK_KHZ_LP_44K (208000)
#define SYS_CLOCK_KHZ_LP_48K (208000)
// 208M8/48k/64 = 67.968->68, 208M8/44k1/64 = 73.979->74

// Core Voltage
//#define V_CORE_HI VREG_VOLTAGE_1_50
#define V_CORE_HI VREG_VOLTAGE_1_25
#define V_CORE_LO VREG_VOLTAGE_1_05


// 初期オーディオサンプル周波数
#define AUDIO_INITIAL_FREQ (44100)

// アップサンプリング倍率(Core0)
#define CORE0_UP_RATIO_MAX ((CORE0_UP_RATIO_HP) > (CORE0_UP_RATIO_LP) ? (CORE0_UP_RATIO_HP) : (CORE0_UP_RATIO_LP))
#define CORE1_UP_RATIO_MAX ((CORE1_UP_RATIO_HP) > (CORE1_UP_RATIO_LP) ? (CORE1_UP_RATIO_HP) : (CORE1_UP_RATIO_LP))

// FIR Filter Type: LINEAR or MINIMUM (Unavailable)
#define LINEAR

// Core1 FIR Filter Mode
#define CORE1_FIR_MODE (CORE1_FIR_MODE_POLYPHASE)

// DCDC Control
#define DCDC_MODE_PIN (23)

// LED
#define ONBOARD_LED_PIN (25)

// オーディオ信号処理周期の設定(timer割り込み処理)
#define TIMER0_US (250)

#define TIMER_US_CORE1 (250)
// Core1 DMA/processing chunk size (us). Larger values give more upsampling time.
#define CORE1_PROCESS_US (2000)

// エンドポイントバッファサイズ((96+1)kHz*1ms=97以上あればよい)
#define SIZE_EP_BUFFER (512)

// アップサンプリングバッファサイズ
#define SIZE_UPSAMPLE_CORE0 (8192) // Core0 Upsampling Buffer Size (samples per channel)

// DMA転送バッファサイズ 3以上あればよい
#define DEPTH_DMA_TX_BUFFER (3)

// FB水位(50%が望ましい)
#define SIZE_BUFFER_FB_THRESHOLD (SIZE_UPSAMPLE_CORE0 / 2)

// Feedback(±1サンプルになる値を返す 基準は1000)
#define FB_ADJ_LIMIT (1000)

// アップサンプリング時のデータサイズ増減幅
#define OSR_ADJ_SIZE (1)

// ボリューム管理
#define VOLUME_RESOLUTION (256)
#define MIN_VOLUME (-64 * VOLUME_RESOLUTION)
#define MAX_VOLUME (0 * VOLUME_RESOLUTION)
#define DEFAULT_VOLUME (0 * VOLUME_RESOLUTION)

// ノンブロッキングI2C
#define SIZE_I2C_RINGBUFFER (16)
#define SIZE_I2C_TRANSFER_UNIT (2)

typedef struct
{
	uint32_t freq;		// 周波数系列軸・倍率軸用(既存)
	uint32_t bit_depth; // ビット深度軸用(追加)
	int16_t now_volume;
	int16_t acq_volume;
	float vol_float;
	int16_t vol_mul;
	uint32_t vol_shift;
	bool mute;
} AUDIO_STATE;

extern RINGBUFFER buffer_ep_Lch;
extern RINGBUFFER buffer_ep_Rch;
extern RINGBUFFER buffer_upsr_data_Lch_0;
extern RINGBUFFER buffer_upsr_data_Rch_0;

extern AUDIO_STATE audio_state;
extern volatile bool is_high_power_mode;
extern uint32_t now_playing;
extern uint16_t length_remain_to_I2S_FIFO;

extern uint32_t calc_pwm_period_us(float period_us, uint16_t prescale);
extern void setup_I2C(void);
extern void volume_control(void);

extern void renew_clock(bool is_high_power);
extern void cancel_timer0(void);
extern void restart_timer0(void);

inline int32_t saturation_i32(int32_t in, int32_t max, int32_t min)
{
	if (in > max)
		return max;
	else if (in < min)
		return min;
	return in;
}

inline float saturation_f32(float in, float max, float min)
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

// アップサンプリング倍率をビットシフト量に変換する関数
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

// アップサンプリング倍率取得関数(Core0)
inline uint16_t get_ratio_upsampling_core0(uint32_t freq)
{
	uint16_t base = is_high_power_mode ? CORE0_UP_RATIO_HP : CORE0_UP_RATIO_LP;
	uint16_t ratio;
	switch (freq)
	{
	case 192000:
	case 176400:
		ratio = base >> 2;
		break;
	case 96000:
	case 88200:
		ratio = base >> 1;
		break;
	case 48000:
	case 44100:
	default:
		ratio = base;
		break;
	}

	if (ratio == 0)
		return 1;
	return ratio;
}

inline uint16_t get_ratio_upsampling_core1(void)
{
	uint16_t ratio = is_high_power_mode ? CORE1_UP_RATIO_HP : CORE1_UP_RATIO_LP;
	if (ratio == 0)
		return 1;
	return ratio;
}

#endif
