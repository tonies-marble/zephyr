/*
 * Copyright (c) 2021 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this file is linked to RAM. Any functions called while preparing for
 * sleep mode must be defined within this file, or linked to RAM.
 */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <fsl_dcdc.h>
#include <fsl_pmu.h>
#include <fsl_gpc.h>
#include <fsl_clock.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>

#include "power.h"

LOG_MODULE_REGISTER(soc_power, CONFIG_SOC_LOG_LEVEL);

#define XTAL_NODE       DT_NODELABEL(xtal)
#define LPM_GPC_IMR_NUM (sizeof(GPC->IMR) / sizeof(GPC->IMR[0]))

static struct clock_callbacks lpm_clock_hooks;

/*
 * Boards with RT10XX SOCs can register callbacks to set their clocks into
 * normal/full speed mode, low speed mode, and low power mode.
 * If callbacks are present, the low power subsystem will disable
 * PLLs for power savings when entering low power states.
 */
void imxrt_clock_pm_callbacks_register(struct clock_callbacks *callbacks)
{
	/* If run callback is set, low power must be as well. */
	__ASSERT_NO_MSG(callbacks && callbacks->clock_set_run && callbacks->clock_set_low_power);
	lpm_clock_hooks.clock_set_run = callbacks->clock_set_run;
	lpm_clock_hooks.clock_set_low_power = callbacks->clock_set_low_power;
	if (callbacks->clock_lpm_init) {
		lpm_clock_hooks.clock_lpm_init = callbacks->clock_lpm_init;
	}
}

static void lpm_set_sleep_mode_config(clock_mode_t mode)
{
	uint32_t clpcr;

	/* Set GPC wakeup config to GPT timer interrupt */
	GPC_EnableIRQ(GPC, DT_IRQN(DT_INST(0, nxp_gpt_hw_timer)));
	/*
	 * ERR050143: CCM: When improper low-power sequence is used,
	 * the SoC enters low power mode before the ARM core executes WFI.
	 *
	 * Software workaround:
	 * 1) Software should trigger IRQ #41 (GPR_IRQ) to be always pending
	 *      by setting IOMUXC_GPR_GPR1_GINT.
	 * 2) Software should then unmask IRQ #41 in GPC before setting CCM
	 *      Low-Power mode.
	 * 3) Software should mask IRQ #41 right after CCM Low-Power mode
	 *      is set (set bits 0-1 of CCM_CLPCR).
	 */
	GPC_EnableIRQ(GPC, GPR_IRQ_IRQn);
	clpcr = CCM->CLPCR & (~(CCM_CLPCR_LPM_MASK | CCM_CLPCR_ARM_CLK_DIS_ON_LPM_MASK));
	/* Note: if CCM_CLPCR_ARM_CLK_DIS_ON_LPM_MASK is set,
	 * debugger will not connect in sleep mode
	 */
	/* Set clock control module to transfer system to idle mode */
	clpcr |= CCM_CLPCR_LPM(mode) | CCM_CLPCR_MASK_SCU_IDLE_MASK |
		 CCM_CLPCR_MASK_L2CC_IDLE_MASK | CCM_CLPCR_STBY_COUNT_MASK |
		 CCM_CLPCR_ARM_CLK_DIS_ON_LPM_MASK;
#ifndef CONFIG_SOC_MIMXRT1011
	/* RT1011 does not include handshake bits */
	clpcr |= CCM_CLPCR_BYPASS_LPM_HS0_MASK | CCM_CLPCR_BYPASS_LPM_HS1_MASK;
#endif
	if (mode == kCLOCK_ModeStop) {
		clpcr |= CCM_CLPCR_VSTBY_MASK | CCM_CLPCR_SBYOS_MASK;
	}
	CCM->CLPCR = clpcr;
	GPC_DisableIRQ(GPC, GPR_IRQ_IRQn);
}

static void lpm_set_standby_config(void)
{
	uint32_t i;
	uint32_t gpcIMR[LPM_GPC_IMR_NUM];
	uint32_t gpcIMR5;

	/* Connect internal the load resistor */
	DCDC->REG1 |= DCDC_REG1_REG_RLOAD_SW_MASK;

	/* Turn off FlexRAM0 */
	GPC->CNTR |= GPC_CNTR_PDRAM0_PGE_MASK;
	/* Turn off FlexRAM1 */
	PGC->MEGA_CTRL |= PGC_MEGA_CTRL_PCR_MASK;

	/* Clean data cache to make sure context is saved into RAM */
	SCB_CleanDCache();

	/* Adjust LP voltage to 0.925V */
	DCDC_AdjustTargetVoltage(DCDC, 0x13, 0x1);
	/* Switch DCDC to use DCDC internal OSC */
	DCDC_SetClockSource(DCDC, kDCDC_ClockInternalOsc);

	/* Power down CPU when requested */
	PGC->CPU_CTRL = PGC_CPU_CTRL_PCR_MASK;

	/* STOP_MODE config, turn off all analog except RTC in stop mode */
	PMU->MISC0_CLR = PMU_MISC0_STOP_MODE_CONFIG_MASK;

	/* Mask all GPC interrupts before enabling the RBC counters to
	 * avoid the counter starting too early if an interupt is already
	 * pending.
	 */
	for (i = 0; i < LPM_GPC_IMR_NUM; i++) {
		gpcIMR[i] = GPC->IMR[i];
		GPC->IMR[i] = 0xFFFFFFFFU;
	}
	gpcIMR5 = GPC->IMR5;
	GPC->IMR5 = 0xFFFFFFFFU;

	/*
	 * ERR006223: CCM: Failure to resuem from wait/stop mode with power gating
	 *   Configure REG_BYPASS_COUNTER to 2
	 *   Enable the RBC bypass counter here to hold off the interrupts. RBC counter
	 *  needs to be no less than 2.
	 */
	CCM->CCR = (CCM->CCR & ~CCM_CCR_REG_BYPASS_COUNT_MASK) | CCM_CCR_REG_BYPASS_COUNT(2);
	CCM->CCR |= (CCM_CCR_OSCNT(0xAF) | CCM_CCR_COSC_EN_MASK | CCM_CCR_RBC_EN_MASK);

	/* Now delay for a short while (3usec) at this point
	 * so a short loop should be enough. This delay is required to ensure that
	 * the RBC counter can start counting in case an interrupt is already pending
	 * or in case an interrupt arrives just as ARM is about to assert DSM_request.
	 */
	SDK_DelayAtLeastUs(3, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

	/* Recover all the GPC interrupts. */
	for (i = 0; i < LPM_GPC_IMR_NUM; i++) {
		GPC->IMR[i] = gpcIMR[i];
	}
	GPC->IMR5 = gpcIMR5;

	// lpm_periph_stop();
}

static void lpm_enter_soft_off_mode(void)
{
	/* Enable the SNVS RTC as a wakeup source from soft-off mode, in case an RTC alarm
	 * was set.
	 */
	GPC_EnableIRQ(GPC, DT_IRQN(DT_INST(0, nxp_imx_snvs_rtc)));
	SNVS->LPCR |= SNVS_LPCR_TOP_MASK;
}

static void lpm_enter_sleep_mode(clock_mode_t mode)
{
	/* FIXME: When this function is entered the Kernel has disabled
	 * interrupts using BASEPRI register. This is incorrect as it prevents
	 * waking up from any interrupt which priority is not 0. Work around the
	 * issue and disable interrupts using PRIMASK register as recommended
	 * by ARM.
	 */

	/* Set PRIMASK */
	__disable_irq();
	/* Set BASEPRI to 0 */
	irq_unlock(0);
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	if (mode == kCLOCK_ModeWait) {
		/* Clear the SLEEPDEEP bit to go into sleep mode (WAIT) */
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	} else {
		/* Set the SLEEPDEEP bit to enable deep sleep mode (STOP) */
		SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
	}

	/* Set Doze bits */
	// IOMUXC_GPR->GPR8 = 0xaaaaaaaa;
	// IOMUXC_GPR->GPR12 = 0x0000000a;

	/* WFI instruction will start entry into WAIT/STOP mode */
	__WFI();
}

static void lpm_set_run_mode_config(void)
{
	/* Clear Doze bits */
	// IOMUXC_GPR->GPR8 = 0;
	// IOMUXC_GPR->GPR12 = 0;

	/* Clear GPC wakeup source */
	GPC_DisableIRQ(GPC, DT_IRQN(DT_INST(0, nxp_gpt_hw_timer)));
	CCM->CLPCR &= ~(CCM_CLPCR_LPM_MASK | CCM_CLPCR_ARM_CLK_DIS_ON_LPM_MASK);
}

/* Toggle the analog bandgap reference circuitry on and off */
static void bandgap_set(bool on)
{
	if (on) {
		/* Enable bandgap in PMU */
		PMU->MISC0_CLR = PMU_MISC0_REFTOP_PWD_MASK;
		/* Wait for it to stabilize */
		while ((PMU->MISC0 & PMU_MISC0_REFTOP_VBGUP_MASK) == 0) {
		}
		/* Disable low power bandgap */
		XTALOSC24M->LOWPWR_CTRL_CLR = XTALOSC24M_LOWPWR_CTRL_LPBG_SEL_MASK;
	} else {
		/* Disable bandgap in PMU and switch to low power one */
		XTALOSC24M->LOWPWR_CTRL_SET = XTALOSC24M_LOWPWR_CTRL_LPBG_SEL_MASK;
		PMU->MISC0_SET = PMU_MISC0_REFTOP_PWD_MASK;
	}
}

/* Should only be used if core clocks have been reduced- drops SOC voltage */
static void lpm_drop_voltage(void)
{
	/* Enable internal RC oscillator, since we are using low power clocks */
	CLOCK_InitRcOsc24M();
	/* Switch to internal RC oscillator */
	CLOCK_SwitchOsc(kCLOCK_RcOsc);
	/* Disable external OSC */
	CLOCK_DeinitExternalClk();
	CLOCK_SetXtal0Freq(0);
	/*
	 * Change to low power SOC voltage. If you are experiencing issues with
	 * low power mode stability, try raising this voltage value.
	 */
	DCDC_AdjustRunTargetVoltage(DCDC, (CONFIG_DCDC_TARGET_LOW_POWER_VOLTAGE - 800) / 25);
	/* Enable 2.5 and 1.1V weak regulators */
	PMU_2P5EnableWeakRegulator(PMU, true);
	PMU_1P1EnableWeakRegulator(PMU, true);
	/* Disable normal regulators */
	PMU_2P5EnableOutput(PMU, false);
	PMU_1P1EnableOutput(PMU, false);
	/* Disable analog bandgap */
	bandgap_set(false);
}

/* Undo the changes made by lpm_drop_voltage so clocks can be raised */
static void lpm_raise_voltage(void)
{
	/* Enable analog bandgap */
	bandgap_set(true);
	/* Enable regulator LDOs */
	PMU_2P5EnableOutput(PMU, true);
	PMU_1P1EnableOutput(PMU, true);
	/* Disable weak LDOs */
	PMU_2P5EnableWeakRegulator(PMU, false);
	PMU_1P1EnableWeakRegulator(PMU, false);
	/* Change to normal SOC voltage */
	DCDC_AdjustRunTargetVoltage(DCDC, (CONFIG_DCDC_TARGET_NORMAL_VOLTAGE - 800) / 25);
	/* Enable external OSC */
	CLOCK_InitExternalClk(0);
	CLOCK_SetXtal0Freq(DT_PROP(XTAL_NODE, clock_frequency));
	/* Switch clock source to external OSC. */
	CLOCK_SwitchOsc(kCLOCK_XtalOsc);

	CLOCK_DeinitRcOsc24M();
}

/* Sets device into low power mode */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		LOG_DBG("entering PM state runtime idle");
		lpm_set_sleep_mode_config(kCLOCK_ModeWait);
		lpm_enter_sleep_mode(kCLOCK_ModeWait);
		break;
	case PM_STATE_SUSPEND_TO_IDLE:
		LOG_DBG("entering PM state suspend to idle");
		if (lpm_clock_hooks.clock_set_low_power) {
			/* Drop the SOC clocks to low power mode, and decrease core voltage */
			lpm_clock_hooks.clock_set_low_power();
			lpm_drop_voltage();
		}
		lpm_set_sleep_mode_config(kCLOCK_ModeWait);
		lpm_enter_sleep_mode(kCLOCK_ModeWait);
		break;
	case PM_STATE_STANDBY:
		LOG_DBG("entering PM state standby");
		if (lpm_clock_hooks.clock_set_low_power) {
			/* Drop the SOC clocks to low power mode, and decrease core voltage */
			lpm_clock_hooks.clock_set_low_power();
			lpm_drop_voltage();
		}
		DCDC_SetClockSource(DCDC, kDCDC_ClockInternalOsc);
		lpm_set_sleep_mode_config(kCLOCK_ModeStop);
		lpm_set_standby_config();
		lpm_enter_sleep_mode(kCLOCK_ModeStop);
		break;
	case PM_STATE_SOFT_OFF:
		LOG_DBG("Entering PM state soft off");
		lpm_enter_soft_off_mode();
		break;
	default:
		return;
	}
}

/* Handle SOC specific activity after Low Power Mode Exit */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	/* Set run mode config after wakeup */
	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		lpm_set_run_mode_config();
		LOG_DBG("exited PM state runtime idle");
		break;
	case PM_STATE_SUSPEND_TO_IDLE:
		lpm_set_run_mode_config();
		if (lpm_clock_hooks.clock_set_run) {
			/* Raise core voltage and restore SOC clocks */
			lpm_raise_voltage();
			lpm_clock_hooks.clock_set_run();
		}
		LOG_DBG("exited PM state suspend to idle");
		break;
	default:
		break;
	}
	/* Clear PRIMASK after wakeup */
	__enable_irq();
}

/* Initialize power system */
void rt10xx_power_init(void)
{
	dcdc_internal_regulator_config_t reg_config;

	/* Ensure clocks to ARM core memory will not be gated in low power mode
	 * if interrupt is pending
	 */
	CCM->CGPR |= CCM_CGPR_INT_MEM_CLK_LPM_MASK;

	if (lpm_clock_hooks.clock_lpm_init) {
		lpm_clock_hooks.clock_lpm_init();
	}

	/* Errata ERR050143 */
	IOMUXC_GPR->GPR1 |= IOMUXC_GPR_GPR1_GINT_MASK;

	/* Initialize GPC to mask all IRQs */
	for (int i = 0; i < (sizeof(GPC->IMR) / sizeof(GPC->IMR[0])); i++) {
		GPC->IMR[i] = 0xFFFFFFFFU;
	}
	GPC->IMR5 = 0xFFFFFFFFU;

	/* Configure DCDC */
	DCDC_BootIntoDCM(DCDC);
	/* Set target voltage for low power mode to 0.925V*/
	DCDC_AdjustLowPowerTargetVoltage(DCDC, 0x1);
	/* Reconfigure DCDC to disable internal load resistor */
	reg_config.enableLoadResistor = false;
	reg_config.feedbackPoint = 0x1; /* 1.0V with 1.3V reference voltage */
	DCDC_SetInternalRegulatorConfig(DCDC, &reg_config);
	DCDC_SetClockSource(DCDC, kDCDC_ClockExternalOsc);

	/* Enable high gate drive on power FETs to reduce leakage current */
	PMU_CoreEnableIncreaseGateDrive(PMU, true);
	// /* Connect vdd_high_in and connect vdd_snvs_in */
	// PMU->MISC0_CLR = PMU_MISC0_DISCON_HIGH_SNVS_MASK;
}
