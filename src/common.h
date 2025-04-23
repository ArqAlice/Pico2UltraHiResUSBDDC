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

// ESS DAC Kind
#define ES9010K2M 0
#define ES9039Q2M 1

// User Configurable ------------------------------------------------------------------

// Faster I2S slew rate
#define I2S_SLEWRATE_FAST_ENABLE (false)

// Enhancement I2S signal output current
#define I2S_STRENGTH_REINFORCE_ENABLE (true)

// Power Mode Switch Pin
#define POWER_MODE_SWITCH_PIN (18)
#define ALWAYS_HIGH_POWER (false)
#define ALWAYS_LOW_POWER (false)

// I2C
#define I2C_PORT (i2c1)
#define I2C_SDA (26)
#define I2C_SCL (27)

// I2S Pin : sideset0:BCLK, sideset1:LRCK (if No Changed)
#define I2S_SIDESET_CHANGE (false)
#define I2S_DATA_PIN (20)
#define I2S_SIDESET_BASE (21)

// Upsampler control
#define BYPASS_CORE1_UPSAMPLING (true)
#define CORE0_UPSAMPLING_192K (false)
#define DEFAULT_GAIN_RATIO (0.6) // Adjust this according to your filter to avoid clipping.

// ESS DAC Specific
#define USE_ESS_DAC (true)
#define KIND_ESS_DAC (ES9039Q2M)
#define I2C_ESS_DAC_ADDR (0x48)
#define DAC_ENABLE_PIN (28)

// User Configurable end ------------------------------------------------------------

// システムクロック
#define SYS_CLOCK_KHZ_44K (283200)
#define SYS_CLOCK_KHZ_48K (307200)
#define SYS_CLOCK_KHZ_LP_44K (208800)
#define SYS_CLOCK_KHZ_LP_48K (208800)
// #define SYS_CLOCK_KHZ 208800 //  208M8/48k/64 = 67.968->68, 208M8/44k1/64 = 73.979->74
// #define SYS_CLOCK_KHZ 150000

// 初期オーディオサンプル周波数
#define AUDIO_INITIAL_FREQ (44100)

// アップサンプリング倍率(Core0)
#define RATIO_UPSAMPLING_48K (8)
#define RATIO_UPSAMPLING_96K (RATIO_UPSAMPLING_48K / 2)
#define RATIO_UPSAMPLING_192K (RATIO_UPSAMPLING_48K / 4)

// アップサンプリング倍率(Core1)
#define RATIO_UPSAMPLING_CORE1 (4)

// DCDC Control
#define DCDC_MODE_PIN (23)

// LED
#define ONBOARD_LED_PIN (25)

// オーディオ信号処理周期の設定(timer割り込み処理)
#define TIMER0_US (250)

#define TIMER_US_CORE1 (250)

// エンドポイントバッファサイズ((96+1)kHz*1ms=97以上あればよい)
#define SIZE_EP_BUFFER (256)

// アップサンプリングバッファサイズ(10ms分程度ほしい (96+1)kHz*10ms*4upsampling=3880 FB水位を50%確保したいのでこれの2倍用意する)
#define SIZE_UPSAMPLE_CORE0 (8192)

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

extern inline int32_t saturation_i32(int32_t in, int32_t max, int32_t min);
extern inline float saturation_f32(float in, float max, float min);
extern inline void int32_to_float_array(int32_t *input, float *output, uint32_t length);
extern inline void float_to_int32_array(float *input, int32_t *output, uint32_t length);
extern inline uint16_t get_ratio_upsampling_core0(uint32_t freq);
extern inline uint16_t get_ratio_upsampling_core1(void);
inline uint16_t ratio_to_bitshift(uint16_t ratio);
extern uint32_t calc_pwm_period_us(float period_us, uint16_t prescale);
extern void setup_I2C(void);
extern void volume_control(void);

extern void renew_clock(bool is_high_power);
extern void cancel_timer0(void);
extern void restart_timer0(void);

#endif