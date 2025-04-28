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

#include "common.h"
#include "transmit_to_dac.h"
#include "upsampling.h"
#include "ringbuffer.h"
#include "debug_with_gpio.h"

void core1_main()
{
	// I2S初期化
	init_i2s_interface();

	while (true)
	{
		dma_tx_start();
		sleep_us(1);
	}
}