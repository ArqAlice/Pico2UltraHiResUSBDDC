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
#include "nonblocking_i2c.h"

// パワー管理
volatile bool is_high_power_mode = true;

// 処理タイミング制御用
// Housekeeping cadence is enforced by an absolute-time gate inside the ISR
// (CORE0_HOUSEKEEPING_US), so timer0's period can vary dynamically
// (CORE0_WFI_PLAY_US while playing / CORE0_WFI_IDLE_US while idle) without
// changing the 500ms housekeeping behavior.

// タイマー割り込み
struct repeating_timer timer0; // デジタルフィルタ演算を割り込みでトリガする

// Dynamic timer0 period + activity tracking for low-power idle
static volatile uint32_t core0_timer_period_us = TIMER0_US;
static volatile uint64_t last_audio_time_us = 0;
static volatile uint64_t next_housekeeping_us = 0;

// ring buffer
RINGBUFFER buffer_ep_Lch;
RINGBUFFER buffer_ep_Rch;
RINGBUFFER buffer_upsr_data_Lch_0;
RINGBUFFER buffer_upsr_data_Rch_0;

// I2C ring buffer
I2C_RINGBUFFER i2c_ringbuffer0;

// Audio State
AUDIO_STATE audio_state;
uint32_t now_playing = 0;
static uint32_t now_playing_old = 0;
static bool is_cleared_buffer = false;

// 出力開始時間
volatile absolute_time_t time_start_output;

bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t);

// I2C送信インターバル
volatile absolute_time_t time_start_i2c_transfer = 0;

// Core1メイン
extern void core1_main();

// ミュート解除タイミング確認用: enable_output は transmit_to_dac.h で extern(volatile) 宣言済み

void cancel_timer0(void)
{
	cancel_repeating_timer(&timer0);
}

void restart_timer0(void)
{
	add_repeating_timer_us(-(int64_t)core0_timer_period_us, core0_timer_callback, NULL, &timer0);
}

// Change timer0's period at runtime. Re-arms the timer only on actual changes,
// so calling this every loop iteration is cheap.
// NOTE: cancel/restart must be atomic here. renew_clock() (called from the
// timer0 ISR and from the USB control ISR) also does cancel_timer0() +
// restart_timer0(); without this critical section, an ISR firing between our
// cancel and restart could leave timer0 registered twice and corrupt the
// repeating-timer list.
static void core0_timer_set_period(uint32_t period_us)
{
	if (period_us == 0 || period_us == core0_timer_period_us)
		return;
	uint32_t save = save_and_disable_interrupts();
	core0_timer_period_us = period_us;
	cancel_timer0();
	restart_timer0();
	restore_interrupts(save);
}

// アップサンプリング処理のタイミングをセットする
bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t)
{
	// ES9038Q2Mの周波数切り替え時のノイズ対策
	if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M && get_ess_dac_mute())
	{
		if (enable_output)
		{
			int64_t elapsed_us = absolute_time_diff_us(time_start_output, get_absolute_time());
			if (elapsed_us > TIME_ES9038Q2M_DEPOP_USEC)
			{
				ess_dac_unmute();
			}
		}
	}

	// Absolute-time gate: housekeeping runs every CORE0_HOUSEKEEPING_US
	// (500ms, identical to the original 2000 ticks * 250us) regardless of
	// the current dynamic timer period.
	uint64_t now_us = time_us_64();
	if ((int64_t)(now_us - next_housekeeping_us) >= 0)
	{
		next_housekeeping_us = now_us + CORE0_HOUSEKEEPING_US;
		// パワーモード切り替え
		if ((gpio_get(POWER_MODE_SWITCH_PIN) || ALWAYS_HIGH_POWER) && (!ALWAYS_LOW_POWER))
		{
			// HiPowerMode
			if (!is_high_power_mode)
			{
				is_high_power_mode = true;
				if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
					ess_dac_mute();
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
				if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
					ess_dac_mute();
				clear_ringbuffer(&buffer_ep_Lch);
				clear_ringbuffer(&buffer_ep_Rch);
				clear_ringbuffer(&buffer_upsr_data_Lch_0);
				clear_ringbuffer(&buffer_upsr_data_Rch_0);
				clear_bq_filter_delay();
				renew_clock(is_high_power_mode);
			}
		}

		gpio_put(ONBOARD_LED_PIN, is_high_power_mode);


		// 再生停止時にアップサンプリングフラグとバッファをクリアする
		if ((now_playing == now_playing_old) && (!is_cleared_buffer))
		{
			clear_ringbuffer(&buffer_ep_Lch);
			clear_ringbuffer(&buffer_ep_Rch);
			clear_ringbuffer(&buffer_upsr_data_Lch_0);
			clear_ringbuffer(&buffer_upsr_data_Rch_0);
			// clear_i2c_ringbuffer(&i2c_ringbuffer0);
			clear_bq_filter_delay();
			// i2c_dma_stop_and_clear();
			renew_clock(is_high_power_mode);
			now_playing = 0;
			is_cleared_buffer = true;
		}
		else if (now_playing != now_playing_old)
		{
			is_cleared_buffer = false;
		}

		now_playing_old = now_playing;
	}
	return true;
}

int main(void)
{
	// 外部電源有効化ピン
	if (USE_EXT_POWER_ENABLE)
	{
		gpio_init(EXT_POWER_ENABLE_PIN);
		gpio_set_dir(EXT_POWER_ENABLE_PIN, GPIO_OUT);
		gpio_put(EXT_POWER_ENABLE_PIN, false);
		sleep_us(BOOT_WAIT_TIME_US);
		gpio_put(EXT_POWER_ENABLE_PIN, true);
	}
	else
	{
		sleep_us(BOOT_WAIT_TIME_US);
	}

	// 動作電圧とクロックを引き上げる
	vreg_set_voltage(V_CORE_HI);
	sleep_ms(2);
	set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
	sleep_us(1);

	stdout_uart_init();

	// テストモード用ピンを有効化
	if (TEST_MODE)
	{
		gpio_init(TEST_PIN1);
		gpio_init(TEST_PIN2);
		gpio_set_dir(TEST_PIN1, GPIO_OUT);
		gpio_set_dir(TEST_PIN2, GPIO_OUT);
	}

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

	// オンボードLED点灯用
	gpio_init(ONBOARD_LED_PIN);
	gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
	gpio_put(ONBOARD_LED_PIN, true);

	// DACチップ制御用I2Cの初期化
	setup_I2C();

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
	{
		ess_dac_i2c_setup();
		initialize_i2c_ringbuffer(SIZE_I2C_RINGBUFFER, &i2c_ringbuffer0);
	}
	// アップサンプリングフィルタを初期化する
	init_upsampling_filter();

	usb_sound_card_init();
	sleep_ms(100);

	// watchdog_enable(50, 1);

	while (true)
	{
		// watchdog_update();

		if (TEST_MODE)
			gpio_put(TEST_PIN1, true);
		upsampling_process_core0();
		if (TEST_MODE)
			gpio_put(TEST_PIN1, false);

		// ESS DAC用I2C送信処理
		if (USE_ESS_DAC)
		{
			int64_t elapsed_us = absolute_time_diff_us(time_start_i2c_transfer, get_absolute_time());
			static int size_transfer = 0;

			if ((!i2c_dma_is_busy()) && (elapsed_us >= I2C_ESS_DAC_TRANSFER_INTERVAL_USEC * (size_transfer + 1)))
			{
				uint16_t size_using = i2c_ringbuf_get_size_using(&i2c_ringbuffer0);
				if (size_using > 0)
				{
					I2C_RB_DATA buffer;
					i2c_ringbuf_read(&buffer, &i2c_ringbuffer0);
					i2c_write_dma(buffer.i2c, buffer.addr_7bit, buffer.data, buffer.len, buffer.nostop);
					size_transfer = buffer.len;
					time_start_i2c_transfer = get_absolute_time();
				}
			}
		}

#if USE_ESS_DAC
		bool ess_pending = (i2c_ringbuf_get_size_using(&i2c_ringbuffer0) > 0) || i2c_dma_is_busy();
#else
		bool ess_pending = false;
#endif

		// Low-power idle: when there is no audio to upsample and no I2C pending,
		// wait-for-interrupt instead of busy-looping. The core wakes on the next
		// event (USB buffer-status IRQ for new data, or timer0 housekeeping at
		// TIMER0_US). System/USB clocks keep running, so enumeration is preserved.
		// Track the last time audio data actually arrived (now_playing is
		// bumped by the USB audio packet handler).
		static uint32_t now_playing_seen = 0;
		if (now_playing != now_playing_seen)
		{
			now_playing_seen = now_playing;
			last_audio_time_us = time_us_64();
		}

		// Dynamic WFI poll period for Core0 (same idea as Core1):
		//   playing (recent packets) -> CORE0_WFI_PLAY_US (250us)
		//   idle (no recent packets) -> CORE0_WFI_IDLE_US (1ms)
		if (ENABLE_LOW_POWER_IDLE_CORE0)
		{
			bool playing = ((int64_t)(time_us_64() - last_audio_time_us) < (int64_t)CORE0_PLAY_DETECT_US);
			core0_timer_set_period(playing ? CORE0_WFI_PLAY_US : CORE0_WFI_IDLE_US);

			if (!ess_pending)
			{
				__wfi();
			}
		}
		else
		{
			sleep_us(100);
		}
	}
}
