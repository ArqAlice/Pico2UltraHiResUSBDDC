/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/regs/intctrl.h"
#include "common.h"
#include "usb_device_control.h"
#include "transmit_to_dac.h"
#include "upsampling.h"
#include "ringbuffer.h"
#include "debug_with_gpio.h"
#include "ess_specific.h"

// パワー管理
volatile bool is_high_power_mode = true;

// 処理タイミング制御用
#define MILLISEC50 (500000 / TIMER0_US)

// タイマー割り込み
struct repeating_timer timer0; // デジタルフィルタ演算を割り込みでトリガする
volatile bool can_proceed_upsampling_core0 = false;

// ring buffer
RINGBUFFER buffer_ep_Lch;
RINGBUFFER buffer_ep_Rch;
RINGBUFFER buffer_upsr_data_Lch_0;
RINGBUFFER buffer_upsr_data_Rch_0;

// Audio State
AUDIO_STATE audio_state;
uint32_t now_playing = 0;

bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t);

// Core1メイン
extern void core1_main();

void cancel_timer0(void)
{
	cancel_repeating_timer(&timer0);
}

void restart_timer0(void)
{
	add_repeating_timer_us(-TIMER0_US, core0_timer_callback, NULL, &timer0);
}

// アップサンプリング処理のタイミングをセットする
bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t)
{
	can_proceed_upsampling_core0 = true;

	volatile static uint32_t now_playing_old = 0;
	static volatile int count = 0;
	count++;
	if (count >= MILLISEC50)
	{
		// パワーモード切り替え
		if ((gpio_get(POWER_MODE_SWITCH_PIN) || ALWAYS_HIGH_POWER) && (!ALWAYS_LOW_POWER) && (!BYPASS_CORE1_UPSAMPLING) && (!CORE0_UPSAMPLING_192K))
		{
			// HiPowerMode
			if (!is_high_power_mode)
			{
				is_high_power_mode = true;
				clear_ringbuffer(&buffer_ep_Lch);
				clear_ringbuffer(&buffer_ep_Rch);
				clear_ringbuffer(&buffer_upsr_data_Lch_0);
				clear_ringbuffer(&buffer_upsr_data_Rch_0);
				clear_bq_filter_delay();
				renew_clock(is_high_power_mode);
			}
		}
		else
		{
			// LoPowerMode
			if (is_high_power_mode)
			{
				is_high_power_mode = false;
				clear_ringbuffer(&buffer_ep_Lch);
				clear_ringbuffer(&buffer_ep_Rch);
				clear_ringbuffer(&buffer_upsr_data_Lch_0);
				clear_ringbuffer(&buffer_upsr_data_Rch_0);
				clear_bq_filter_delay();
				renew_clock(is_high_power_mode);
			}
		}

		gpio_put(ONBOARD_LED_PIN, is_high_power_mode);

		volume_control();

		// 再生停止時にアップサンプリングフラグとバッファをクリアする
		if (now_playing == now_playing_old)
		{
			clear_ringbuffer(&buffer_ep_Lch);
			clear_ringbuffer(&buffer_ep_Rch);
			clear_ringbuffer(&buffer_upsr_data_Lch_0);
			clear_ringbuffer(&buffer_upsr_data_Rch_0);
			clear_bq_filter_delay();
			renew_clock(is_high_power_mode);
			now_playing = 0;
		}
		now_playing_old = now_playing;

		count = 0;
	}
	return true;
}

int main(void)
{
	set_sys_clock_48mhz();
	sleep_ms(2);
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
	sleep_ms(2);

	stdout_uart_init();

	// 各種バッファ初期化
	initialize_ringbuffer(SIZE_EP_BUFFER, true, &buffer_ep_Lch);				// USB EP受け取り用
	initialize_ringbuffer(SIZE_EP_BUFFER, true, &buffer_ep_Rch);				// USB EP受け取り用
	initialize_ringbuffer(SIZE_UPSAMPLE_CORE0, false, &buffer_upsr_data_Lch_0); // Core1転送用
	initialize_ringbuffer(SIZE_UPSAMPLE_CORE0, false, &buffer_upsr_data_Rch_0); // Core1転送用

	// オーディオステータス初期化
	audio_state.freq = AUDIO_INITIAL_FREQ;
	audio_state.bit_depth = 16;
	audio_state.mute = false;

	// パワーモード切り替え用
	gpio_init(POWER_MODE_SWITCH_PIN);
	gpio_set_dir(POWER_MODE_SWITCH_PIN, GPIO_IN);
	gpio_pull_down(POWER_MODE_SWITCH_PIN);

	// DACチップ制御用I2Cの初期化
	setup_I2C();

	// DCDCの動作モード、trueでFPWM、falseでPFM
	gpio_init(DCDC_MODE_PIN);
	gpio_set_dir(DCDC_MODE_PIN, true);
	gpio_put(DCDC_MODE_PIN, true);

	// ESS DACを初期化
	if (USE_ESS_DAC)
	{
		ess_dac_initialize();
		sleep_ms(10);
	}

	// アップサンプリング処理用Timer割り込みをアタッチする
	add_repeating_timer_us(-TIMER0_US, core0_timer_callback, NULL, &timer0);

	// Core1を起動する Core1ではI2S出力処理をしている
	multicore_launch_core1(core1_main);

	// Activate DAC
	if (USE_ESS_DAC)
	{
		ess_dac_activate();
		sleep_ms(10);
	}

	// ESS DAC SETUP
	if (USE_ESS_DAC)
		ess_dac_i2c_setup();

	// アップサンプリングフィルタを初期化する
	init_upsampling_filter();

	// オンボードLED点灯用
	gpio_init(25);
	gpio_set_dir(25, GPIO_OUT);

	// デバッグ用
	gpio_init(2);
	gpio_set_dir(2, GPIO_OUT);

	usb_sound_card_init();
	sleep_ms(100);

	// watchdog_enable(50, 1);

	while (true)
	{
		// watchdog_update();

		if (can_proceed_upsampling_core0)
		{
			can_proceed_upsampling_core0 = false;
			gpio_put(2, true);
			upsampling_process_core0();
			gpio_put(2, false);
		}
		sleep_us(1);
	}
}
