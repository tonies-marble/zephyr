/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * How long a sleep lasted, measured with the always-on ameba system timer.
 *
 * Shared by both PM paths -- common/pm.c on the Cortex-M SoCs and amebasmart/pm.c
 * on the CA32 -- because the measurement is the same on all of them: TIM0 keeps
 * running through every sleep state, so sampling it either side gives the gated
 * duration. Only the entry halves differ (the Cortex-M SoCs also hand the kernel's
 * deadline to the PMC wake timer, which the CA32 cannot), so each keeps its own
 * z_sys_clock_lpm_enter() and calls in here for the sampling.
 */

#include <ameba_soc.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ameba_lpm_timer.h"

LOG_MODULE_REGISTER(ameba_lpm_timer, CONFIG_PM_LOG_LEVEL);

static uint32_t entry_tick;
/*
 * Sub-microsecond remainder of the last conversion, in system-timer ticks scaled
 * by USEC_PER_SEC. Carried so that a workload which sleeps continuously does not
 * accumulate the truncation into a drifting clock (as the hal carries missing_tick).
 */
static uint32_t conv_remainder;

void ameba_lpm_timer_mark_entry(void)
{
	entry_tick = SYSTIMER_TickGet();
}

uint64_t ameba_lpm_timer_elapsed_us(void)
{
	uint32_t now = SYSTIMER_TickGet();
	uint64_t scaled;
	uint64_t elapsed_us;

	/*
	 * SYSTIMER_GetPassTick() is deliberately not used for the subtraction. The
	 * hal implementation treats now < entry as a counter wrap and returns
	 * 0xFFFFFFFF - (entry - now), which is right for a free-running counter and
	 * catastrophic for one that got reset behind our back: it reports ~2^32
	 * ticks, i.e. 2^32/32768 = 36:24:32 of sleep, after which every k_timeout_t
	 * is already expired. That is not hypothetical -- TIM0 is also TIMER0, which
	 * the dts exposes as a Zephyr counter device, and an application driving it
	 * as one resets and stops it. A reading that has not moved forward credits
	 * nothing instead; an announcement cannot be taken back. A real wrap takes a
	 * single sleep of 36 hours and is not worth distinguishing.
	 */
	if (now < entry_tick) {
		LOG_WRN("system timer went backwards (%u -> %u), sleep time lost", entry_tick, now);
		return 0;
	}

	scaled = (uint64_t)(now - entry_tick) * USEC_PER_SEC + conv_remainder;
	elapsed_us = scaled / AMEBA_SYSTIMER_HZ;
	conv_remainder = (uint32_t)(scaled % AMEBA_SYSTIMER_HZ);

	return elapsed_us;
}
