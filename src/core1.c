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
#include "hardware/vreg.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "common.h"
#include "transmit_to_dac.h"
#include "upsampling.h"
#include "ringbuffer.h"
#include "debug_with_gpio.h"

// Core1 wake-up alarm pool.
//
// IMPORTANT (why a dedicated pool is required):
// The default alarm pool is backed by hardware alarm 0 (TIMER_IRQ_0). Its
// handler dispatch and irq_set_enabled() live on whichever core created it
// (Core0 here, via timer0), and irq_set_enabled() only affects the CALLING
// core's NVIC on RP2350. A timer on the default pool therefore dispatches on
// Core0 and NEVER wakes Core1 from WFI (this was the original failure: Core1
// slept forever, DMA starved, no I2S output).
//
// Fix: create a dedicated pool on an otherwise-unused hardware alarm (2 ->
// TIMER_IRQ_2) WHILE RUNNING ON CORE1. irq_add_shared_handler/irq_set_enabled
// then take effect in Core1's NVIC, so the callback runs on Core1 and WFI is
// released.
//
// The wake source is a ONE-SHOT alarm armed just before each WFI, so the poll
// period can vary by state:
//   - output quiesced (stopped/idle): CORE1_WFI_IDLE_US (1ms)
//   - playing but nothing to submit (TX ring full): CORE1_WFI_PLAY_US (250us)
//     (DMA completion is polled, not IRQ-driven, so we must keep waking
//      promptly to re-arm DMA; 250us is far below one 2ms chunk => safe)
#define CORE1_WAKE_ALARM (2u)
static alarm_pool_t *core1_wake_pool = NULL;

// One-shot wake callback: does nothing but generate the Core1 TIMER_IRQ_2
// event that releases __wfi(). Returning 0 => alarm does not repeat.
static int core1_wake_callback(__unused alarm_id_t id, __unused void *user_data)
{
	return 0;
}

void core1_main()
{
	// I2S初期化
	init_i2s_interface();

	if (ENABLE_LOW_POWER_IDLE_CORE1)
	{
		core1_wake_pool = alarm_pool_create(CORE1_WAKE_ALARM, 1);
	}

	while (true)
	{
		if(TEST_MODE) gpio_put(TEST_PIN2, true);
		dma_tx_start();
		if(TEST_MODE) gpio_put(TEST_PIN2, false);

		if (ENABLE_LOW_POWER_IDLE_CORE1 && core1_wake_pool)
		{
			bool quiesced = dma_tx_is_quiesced();
			bool nothing_to_submit = dma_tx_no_submit_pending();

			// Only WFI when there is genuinely nothing to do right now:
			//  - output fully stopped (idle), or
			//  - playing but the TX ring is full (waiting for DMA completion).
			// Otherwise the core must keep servicing DMA submission without
			// sleeping (underrun => audible glitch).
			if (quiesced || nothing_to_submit)
			{
				uint64_t poll_us = quiesced ? CORE1_WFI_IDLE_US : CORE1_WFI_PLAY_US;
				alarm_pool_add_alarm_at(core1_wake_pool,
				                        delayed_by_us(get_absolute_time(), poll_us),
				                        core1_wake_callback, NULL, true);
				__wfi();
			}
			else
			{
				sleep_us(5);
			}
		}
		else
		{
			sleep_us(5);
		}
	}
}