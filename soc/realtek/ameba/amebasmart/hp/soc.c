/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <soc.h>
#include <ameba_soc.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>

void z_arm_reset(void);

IMAGE2_ENTRY_SECTION
RAM_START_FUNCTION Img2EntryFun0 = {z_arm_reset, NULL, /* BOOT_RAM_WakeFromPG, */
				    (uint32_t)NewVectorTable};

void soc_early_init_hook(void)
{
	/*
	 * Cache is enabled by default at reset, disable it before
	 * sys_cache*-functions can enable them.
	 */
	Cache_Enable(DISABLE);
	sys_cache_instr_enable();
	sys_cache_data_enable();

	XTAL_INIT();

	/* Only ASIC silicon needs OSC calibration. Matches
	 * ameba-rtos/component/soc/amebasmart/fwlib/ram_hp/ameba_app_start.c.
	 */
	if (SYSCFG_CHIPType_Get() == CHIP_TYPE_ASIC) {
		if (!(BOOT_Reason() & AON_BIT_RSTF_DSLP)) {
			OSC131K_Calibration(30000); /* PPM=30000=3% */
		}
		OSC4M_Calibration(30000);
		XTAL_PDCK();
	}
}

/*
 * The FPU/SFPA teardown fix on thread abort lives in the HAL's
 * thread_abort_hook (modules/hal_realtek/ameba/os_wrapper/os_wrapper_task.c)
 * since a hook already exists there. See that file for the details.
 */

/*
 * z_arm_on_enter_cpu_idle: currently a no-op returning true so the
 * default WFI runs. The hook is kept wired (selected in Kconfig) so a
 * future power-management pass can gate WFI on wake-source readiness
 * without another SoC change.
 */
bool z_arm_on_enter_cpu_idle(void)
{
	return true;
}
