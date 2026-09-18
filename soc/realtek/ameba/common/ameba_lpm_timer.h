/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_REALTEK_AMEBA_COMMON_AMEBA_LPM_TIMER_H_
#define ZEPHYR_SOC_REALTEK_AMEBA_COMMON_AMEBA_LPM_TIMER_H_

#include <zephyr/types.h>

/* The always-on ameba system timer (TIM0) runs from the 32.768 kHz clock. */
#define AMEBA_SYSTIMER_HZ 32768U

/**
 * @brief Sample the always-on timer on the way into a sleep state.
 *
 * Called from the SoC's z_sys_clock_lpm_enter(), while the reference can still be
 * read.
 */
void ameba_lpm_timer_mark_entry(void);

/**
 * @brief Time since the matching ameba_lpm_timer_mark_entry(), in microseconds.
 *
 * Called from the SoC's z_sys_clock_lpm_exit(). Returns 0 if the reference did not
 * move forward, which credits nothing rather than crediting a bogus interval.
 */
uint64_t ameba_lpm_timer_elapsed_us(void);

#endif /* ZEPHYR_SOC_REALTEK_AMEBA_COMMON_AMEBA_LPM_TIMER_H_ */
