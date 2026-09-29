/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal system support for the RTL8730E KM0 low-power core
 * (Real-M200, Cortex-M23 / Armv8-M baseline).
 */

#include <stdint.h>
#include <ameba_soc.h>

/* KM0 LP core clock: XTAL 40 MHz (live rate re-readable via CPU_ClkGet()). */
uint32_t SystemCoreClock = 40000000U;

extern void z_arm_reset(void);
void km0_boot_trampoline(void);

/*
 * KM0 boot entry table at ORIGIN(RAM) (0x23002020, pinned via
 * km0/image2_entry_section.ld).  The LP boot ROM enters the loaded image
 * through this vendor 8-field RAM_FUNCTION_START_TABLE (as bootloader_lp.c's
 * RamStartTable), not the Cortex-M reset vector: cold boot calls FlashStartFun
 * (+0x14), wake-from-powergate calls RamWakeupFun (+0x04).  Point every
 * plausible entry at the trampoline; an unpopulated field means the ROM
 * branches to 0 and hard-faults (HW-confirmed).
 */
__attribute__((used, section(".image2.entry.data")))
const RAM_FUNCTION_START_TABLE km0_image2_entry_tbl = {
	.RamStartFun   = km0_boot_trampoline,
	.RamWakeupFun  = km0_boot_trampoline,
	.FlashStartFun = km0_boot_trampoline,
};

/*
 * Pre-Zephyr entry: our image occupies the LP bootloader's slot, so this
 * stands in for the vendor BOOT_Image1 pre-C prologue (select LP SoC clock =
 * XTAL, enable I/D caches) before handing off to Zephyr startup.  Relies on
 * the ROM-provided MSP, like the vendor code.  Placed in ".image2.entry.text"
 * (low SRAM, right after the table) to match the vendor layout.
 */
__attribute__((used, section(".image2.entry.text")))
void km0_boot_trampoline(void)
{
	u32 reg;

	/* LP-domain SoC clock source = XTAL. */
	reg = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LSYS_CKSL_GRP0);
	reg |= LSYS_CKSL_LSOC(BIT_LSYS_CKSL_LP_XTAL);
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_LSYS_CKSL_GRP0, reg);

	/*
	 * Drop stale I-cache lines before enabling: SCB_EnableICache() skips its
	 * invalidate if the ROM left the cache on.
	 */
	ICache_Invalidate();
	Cache_Enable(ENABLE);

	z_arm_reset(); /* sets MSP/VTOR; never returns */
}

/*
 * Mandatory once the series selects CONFIG_SOC_EARLY_INIT_HOOK; the LP clock
 * tree is already set up by the trampoline, so nothing to do yet.
 */
void soc_early_init_hook(void)
{
}

/* Vendor ram_lp routines the prebuilt LP libraries import; the nuwa HAL
 * module does not ship the ram_lp sources (transcribed from the SDK).
 */
u32 CPU_InInterrupt(void)
{
	return __get_IPSR() != 0;
}

u32 np_status_on(void)
{
	return (HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LSYS_CKE_GRP0) & APBPeriph_NP_CLOCK) ? 1 : 0;
}

/*
 * Vendor IPC dispatcher bring-up (RX handlers come from prebuilt libraries'
 * .ipc.table.data entries).  The rpmsg MBOX driver, when present, owns the
 * IPCLP block and this interrupt line instead.
 */
#ifndef CONFIG_MBOX_REALTEK_AMEBA_IPC
#include <zephyr/init.h>
#include <zephyr/irq.h>

static int soc_ipc_irq_init(void)
{
	ipc_table_init(IPCLP_DEV);
	IRQ_CONNECT(IPC_IRQ, INT_PRI_MIDDLE, IPC_INTHandler, (uint32_t)IPCLP_DEV, 0);
	irq_enable(IPC_IRQ);

	return 0;
}
SYS_INIT(soc_ipc_irq_init, PRE_KERNEL_2, 0);
#else
BUILD_ASSERT(!IS_ENABLED(CONFIG_WIFI_FW_EN),
	     "CONFIG_WIFI_FW_EN needs the vendor IPC dispatcher, which the "
	     "rpmsg MBOX driver replaces on the shared LP IPC interrupt");
#endif
