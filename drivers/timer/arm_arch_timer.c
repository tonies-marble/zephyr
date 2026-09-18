/*
 * Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/init.h>
#include <zephyr/drivers/timer/arm_arch_timer.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include <zephyr/sys_clock.h>
#include <zephyr/arch/cpu.h>
#if !defined(CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE)
#include <zephyr/drivers/timer/system_timer_lpm.h>
#endif

#ifdef CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME
/* precompute CYC_PER_TICK at driver init to avoid runtime double divisions */
static uint32_t cyc_per_tick;
#define CYC_PER_TICK cyc_per_tick
#else
#define CYC_PER_TICK (uint32_t)(sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#endif

#if defined(CONFIG_GDBSTUB)
/* When interactively debugging, the cycle diff can overflow 32-bit variable */
#define cycle_diff_t uint64_t
#else
/* the unsigned long cast limits divisors to native CPU register width */
#define cycle_diff_t unsigned long
#endif
#define CYCLE_DIFF_MAX (~(cycle_diff_t)0)

/*
 * We have two constraints on the maximum number of cycles we can wait for.
 *
 * 1) sys_clock_announce() accepts at most INT32_MAX ticks.
 *
 * 2) The number of cycles between two reports must fit in a cycle_diff_t
 *    variable before converting it to ticks.
 *
 * Then:
 *
 * 3) Pick the smallest between (1) and (2).
 *
 * 4) Take into account some room for the unavoidable IRQ servicing latency.
 *    Let's use 3/4 of the max range.
 *
 * Finally let's add the LSB value to the result so to clear out a bunch of
 * consecutive set bits coming from the original max values to produce a
 * nicer literal for assembly generation.
 */
#define CYCLES_MAX_1 ((uint64_t)INT32_MAX * (uint64_t)CYC_PER_TICK)
#define CYCLES_MAX_2 ((uint64_t)CYCLE_DIFF_MAX)
#define CYCLES_MAX_3 MIN(CYCLES_MAX_1, CYCLES_MAX_2)
#define CYCLES_MAX_4 (CYCLES_MAX_3 / 2 + CYCLES_MAX_3 / 4)
#define CYCLES_MAX_5 (CYCLES_MAX_4 + LSB_GET(CYCLES_MAX_4))

#ifdef CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME
/* precompute CYCLES_MAX at driver init to avoid runtime double divisions */
static uint64_t cycles_max;
#define CYCLES_MAX cycles_max
#else
#define CYCLES_MAX CYCLES_MAX_5
#endif

static uint64_t last_cycle;
static uint64_t last_tick;
static uint32_t last_elapsed;

#if defined(CONFIG_SYSTEM_TIMER_RESET_BY_LPM)
/*
 * Running total of what the counter itself no longer accounts for, because a
 * low-power mode placed it under reset. Added to every cycle-count read so that
 * k_cycle_get_*() stays monotonic across such a mode instead of stepping back
 * to ~0.
 */
static uint64_t cycles_lost_to_reset;
#else
#define cycles_lost_to_reset 0U
#endif

#if !defined(CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE)
/* Set between handing timekeeping to the companion and taking it back. */
static bool timeout_idle;
/* Counter value when it was handed over, which a reset would otherwise lose. */
static uint64_t cycle_pre_idle;
#endif

#if defined(CONFIG_TEST)
const int32_t z_sys_timer_irq_for_test = ARM_ARCH_TIMER_IRQ;
#endif

static void arm_arch_timer_compare_isr(const void *arg)
{
	ARG_UNUSED(arg);

	k_spinlock_key_t key = sys_clock_lock();

#ifdef CONFIG_ARM_ARCH_TIMER_ERRATUM_740657
	/*
	 * Workaround required for Cortex-A9 MPCore erratum 740657
	 * comp. ARM Cortex-A9 processors Software Developers Errata Notice,
	 * ARM document ID032315.
	 */

	if (!arm_arch_timer_get_int_status()) {
		/*
		 * If the event flag is not set, this is a spurious interrupt.
		 * DO NOT modify the compare register's value, DO NOT announce
		 * elapsed ticks!
		 */
		sys_clock_unlock(key);
		return;
	}
#endif /* CONFIG_ARM_ARCH_TIMER_ERRATUM_740657 */

	uint64_t curr_cycle = arm_arch_timer_count();
	uint64_t delta_cycles = curr_cycle - last_cycle;
	uint32_t delta_ticks = (cycle_diff_t)delta_cycles / CYC_PER_TICK;

	/* sys_clock_lock serialises this ISR against itself, so a second CPU
	 * reads the updated last_cycle and computes delta==0.  CNTP_CVAL is
	 * banked per CPU, each CPU re-arms its own below.
	 */
	last_cycle += (cycle_diff_t)delta_ticks * CYC_PER_TICK;
	last_tick += delta_ticks;
	last_elapsed = 0;

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		uint64_t next_cycle = last_cycle + CYC_PER_TICK;

		arm_arch_timer_set_compare(next_cycle);
		arm_arch_timer_set_irq_mask(false);
	} else {
		arm_arch_timer_set_irq_mask(true);
#ifdef CONFIG_ARM_ARCH_TIMER_ERRATUM_740657
		/*
		 * In tickless mode, the compare register is normally not
		 * updated from within the ISR. Yet, to work around the timer's
		 * erratum, a new value *must* be written while the interrupt
		 * is being processed before the interrupt is acknowledged
		 * by the handling interrupt controller.
		 */
		arm_arch_timer_set_compare(~0ULL);
	}

	/*
	 * Clear the event flag so that in case the erratum strikes (the timer's
	 * vector will still be indicated as pending by the GIC's pending register
	 * after this ISR has been executed) the error will be detected by the
	 * check performed upon entry of the ISR -> the event flag is not set,
	 * therefore, no actual hardware interrupt has occurred.
	 */
	arm_arch_timer_clear_int_status();
#else
	}
#endif /* CONFIG_ARM_ARCH_TIMER_ERRATUM_740657 */

	sys_clock_announce_locked(delta_ticks, key);
}

#if !defined(CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE)
/*
 * Hand timekeeping to the low-power companion.
 *
 * Nothing is announced from here: sys_clock_set_timeout() is called with the
 * timeout subsystem's lock held, so announcing would recurse on it. The counter
 * value is only recorded, and sys_clock_idle_exit() announces everything at once.
 *
 * Under SMP the kernel idles each CPU separately, so this can run more than once
 * before the state is actually entered, and each time re-opens the window: the
 * counter reading is the one the CPU that got here last took, which is the closest
 * to when the state was really entered.
 */
static void arch_timer_lpm_enter(uint64_t timeout_us)
{
	timeout_idle = true;
	cycle_pre_idle = arm_arch_timer_count();

	z_sys_clock_lpm_enter(timeout_us);
}

/*
 * Take timekeeping back from the companion, which reports how long the low-power
 * mode lasted.
 *
 * Without CONFIG_SYSTEM_TIMER_RESET_BY_LPM the counter kept running and already
 * accounts for that time, so the ordinary paths cover it. With it, the counter came
 * back from reset and two things have to be repaired.
 *
 * The accounting first: last_cycle and last_tick describe a counter that no longer
 * exists, and the invariant last_cycle == last_tick * CYC_PER_TICK has to hold
 * against the new one, or the next sys_clock_elapsed() underflows and
 * sys_clock_set_timeout() programs a garbage compare. Both are re-based onto the
 * post-reset counter and the timer is restarted.
 *
 * Then the time itself: what the counter had accumulated since the last announced
 * tick before it was reset -- otherwise lost, and in a tickless system with no
 * timeout pending that is the whole interval since the last announcement -- plus
 * what the companion measured while it was down. It cannot be left for
 * sys_clock_elapsed() to report, the way it would be on a platform whose counter
 * merely stopped: a re-based last_cycle can only sit as far behind the counter as
 * the counter has already run since its reset, which is a few ticks, while the owed
 * time is however long the platform slept. So it is announced here, as
 * cortex_m_systick does for the same reason.
 *
 * Announcing needs the calling CPU to be pinned, which is why this holds the timeout
 * lock across it: k_spin_lock() locks interrupts, and a CPU with interrupts locked is
 * not migratable (see z_smp_cpu_mobile()). The SoC's pm_state_exit_post_ops() has
 * re-enabled them by the time the kernel calls this -- it is required to -- so
 * announcing without the lock would be an assert away from a crash under SMP.
 * sys_clock_announce_locked() takes the key and releases it, as the ISR above does.
 */
void sys_clock_idle_exit(void)
{
	uint64_t lpm_time_us;
	uint64_t lpm_cycles;
	uint32_t pre_idle_ticks;
	uint32_t dticks;
	k_spinlock_key_t key;
	uint64_t curr_cycle;

	/*
	 * The same lock the ISR takes, because this touches the same accounting and
	 * has the same reason to exclude other CPUs -- not merely local interrupts.
	 * The kernel calls this on every CPU leaving the state, so under SMP the
	 * claim below decides which one does the work, and a claim that is not
	 * atomic across CPUs decides nothing: both would see the flag set, both
	 * would ask the companion, and a provider measuring against a fixed
	 * reference would report the same interval to each, crediting the state
	 * twice over.
	 */
	key = sys_clock_lock();

	if (!timeout_idle) {
		sys_clock_unlock(key);
		return;
	}
	timeout_idle = false;

	lpm_time_us = z_sys_clock_lpm_exit();

	if (!IS_ENABLED(CONFIG_SYSTEM_TIMER_RESET_BY_LPM)) {
		sys_clock_unlock(key);
		return;
	}

	lpm_cycles = (lpm_time_us * sys_clock_hw_cycles_per_sec()) / USEC_PER_SEC;

	curr_cycle = arm_arch_timer_count();
	pre_idle_ticks = (cycle_diff_t)(cycle_pre_idle - last_cycle) / CYC_PER_TICK;
	dticks = pre_idle_ticks +
		 (uint32_t)((lpm_time_us * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / USEC_PER_SEC);

	/*
	 * The cycle count read cycles_lost_to_reset + cycle_pre_idle on the way in, so it has
	 * to read at least that much plus the companion's measurement now.
	 */
	cycles_lost_to_reset += cycle_pre_idle + lpm_cycles - curr_cycle;

	/* Re-based onto the counter that exists now, at a tick boundary. */
	last_cycle = (curr_cycle / CYC_PER_TICK) * CYC_PER_TICK;
	last_tick = last_cycle / CYC_PER_TICK;
	last_elapsed = 0;

	arm_arch_timer_set_compare(last_cycle + CYC_PER_TICK);
	arm_arch_timer_enable(true);
	arm_arch_timer_set_irq_mask(false);

	/* Releases the lock. */
	sys_clock_announce_locked((int32_t)dticks, key);
}
#endif /* !CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE */

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	if (idle && ticks == K_TICKS_FOREVER) {
		return;
	}

#if !defined(CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE)
	if (idle) {
		arch_timer_lpm_enter(((uint64_t)ticks * USEC_PER_SEC) /
				     CONFIG_SYS_CLOCK_TICKS_PER_SEC);
		return;
	}
#endif

	uint64_t next_cycle;

	if (ticks == K_TICKS_FOREVER) {
		next_cycle = last_cycle + CYCLES_MAX;
	} else {
		next_cycle = (last_tick + last_elapsed + ticks) * CYC_PER_TICK;
		if ((next_cycle - last_cycle) > CYCLES_MAX) {
			next_cycle = last_cycle + CYCLES_MAX;
		}
	}

	arm_arch_timer_set_compare(next_cycle);
	arm_arch_timer_set_irq_mask(false);
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0;
	}

	uint64_t curr_cycle = arm_arch_timer_count();
	uint64_t delta_cycles = curr_cycle - last_cycle;
	uint32_t delta_ticks = (cycle_diff_t)delta_cycles / CYC_PER_TICK;

	last_elapsed = delta_ticks;
	return delta_ticks;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return (uint32_t)(cycles_lost_to_reset + arm_arch_timer_count());
}

uint64_t sys_clock_cycle_get_64(void)
{
	return cycles_lost_to_reset + arm_arch_timer_count();
}

#ifdef CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT
void arch_busy_wait(uint32_t usec_to_wait)
{
	if (usec_to_wait == 0) {
		return;
	}

	uint64_t start_cycles = arm_arch_timer_count();

	uint64_t cycles_to_wait = sys_clock_hw_cycles_per_sec() / USEC_PER_SEC * usec_to_wait;

	for (;;) {
		uint64_t current_cycles = arm_arch_timer_count();

		/* this handles the rollover on an unsigned 32-bit value */
		if ((current_cycles - start_cycles) >= cycles_to_wait) {
			break;
		}
	}
}
#endif

#ifdef CONFIG_SMP
void smp_timer_init(void)
{
	/*
	 * Set compare to the next tick boundary after the current counter.
	 * Using last_cycle (set by the primary core) risks pointing to an
	 * already-elapsed deadline if the secondary core starts up late.
	 */
	arm_arch_timer_set_compare(((arm_arch_timer_count() + CYC_PER_TICK) / CYC_PER_TICK) *
				   CYC_PER_TICK);
	arm_arch_timer_enable(true);
	irq_enable(ARM_ARCH_TIMER_IRQ);
	arm_arch_timer_set_irq_mask(false);
}
#endif

static int sys_clock_driver_init(void)
{

	IRQ_CONNECT(ARM_ARCH_TIMER_IRQ, ARM_ARCH_TIMER_PRIO, arm_arch_timer_compare_isr, NULL,
		    ARM_ARCH_TIMER_FLAGS);
	arm_arch_timer_init();
#ifdef CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME
	cyc_per_tick = sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	cycles_max = CYCLES_MAX_5;
#endif
	last_tick = arm_arch_timer_count() / CYC_PER_TICK;
	last_cycle = last_tick * CYC_PER_TICK;
	arm_arch_timer_set_compare(last_cycle + CYC_PER_TICK);
	arm_arch_timer_enable(true);
	irq_enable(ARM_ARCH_TIMER_IRQ);
	arm_arch_timer_set_irq_mask(false);

	return 0;
}

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
