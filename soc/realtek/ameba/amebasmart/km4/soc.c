/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SoC hooks for the RTL8730E KM4 (Cortex-M55 / Armv8.1-M) Zephyr application
 * (non-secure image2).
 *
 * The KM4 core is released by the secure ROM / secure bootloader with the PLL,
 * PSRAM/DDR controller and pin-mux already brought up, and enters this image
 * through the standard Cortex-M reset vector (z_arm_reset) -- so, unlike the
 * vendor SDK's ameba_app_start.c, this file does NOT re-run clock/XTAL/OSC/PSRAM
 * bring-up.  It provides only what the Zephyr KM4 image itself needs:
 *   - SystemCoreClock (+ update) for the Realtek HAL that is linked in.
 *   - I/D cache enable at the earliest kernel init level.
 */

#include <soc.h>
#include <ameba_soc.h>

#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

/*
 * Ameba "image2 entry header".
 *
 * The vendor IMG1 bootloader enters this non-secure image2 NOT through the
 * Cortex-M reset vector but through Img2EntryFun0, a RAM_START_FUNCTION struct
 * it reads from the first bytes of the loaded image (== ORIGIN(RAM), the
 * KM4_BD_DRAM sub-image load address 0x60000020).  IMAGE2_ENTRY_SECTION places
 * this struct in ".image2.entry.data", which km4/image2_entry_section.ld pins to
 * the start of RAM.  IMG1 calls RamStartFun to start the image.
 *
 * Unlike the amebag2 port, the amebasmart HAL exposes neither RomVectorTable nor
 * SOCPS_WakeFromPG_AP for this core, so:
 *   - RamStartFun / RamWakeupFun both use Zephyr's reset entry z_arm_reset
 *     (this app does not use the ROM PG warm-boot resume path);
 *   - VectorNS points at Zephyr's own vector table (_vector_start).
 */
extern void z_arm_reset(void);
extern char _vector_start[];

IMAGE2_ENTRY_SECTION
RAM_START_FUNCTION Img2EntryFun0 = {
	z_arm_reset,
	z_arm_reset,
	(uint32_t)_vector_start,
};

/*
 * System core clock, mirrored from the vendor ameba_system.c.  The KM4 comes up
 * at 200 MHz out of the bootloader; SystemCoreClockUpdate() refreshes it from
 * the live clock tree.  Kept here (rather than pulling in the CA32-only
 * ram_hp/ameba_system.c) because parts of the Realtek HAL / lib_chipinfo.a
 * reference this symbol.
 */
uint32_t SystemCoreClock = 200000000U;

void SystemCoreClockUpdate(void)
{
	SystemCoreClock = CPU_ClkGet();
}

/* Vendor ram_hp routine the prebuilt WiFi libraries import; the nuwa HAL
 * module does not ship the ram_hp system source for this core.
 */
u32 CPU_InInterrupt(void)
{
	return __get_IPSR() != 0;
}

/*
 * Earliest SoC bring-up (called unconditionally from z_cstart(), before device
 * init).  Refresh SystemCoreClock from the live clock tree and enable the
 * Cortex-M55 L1 instruction and data caches through Zephyr's cache framework
 * (CONFIG_CACHE_MANAGEMENT is default-y for this SoC).
 */
void soc_early_init_hook(void)
{
	SystemCoreClockUpdate();

	sys_cache_instr_enable();
	sys_cache_data_enable();
}

/*
 * Vendor IPC dispatcher bring-up (RX handlers come from prebuilt libraries'
 * .ipc.table.data entries).  The rpmsg MBOX driver, when present, owns the
 * IPCNP block and this interrupt line instead.
 */
#ifndef CONFIG_MBOX_REALTEK_AMEBA_IPC
static int soc_ipc_irq_init(void)
{
	ipc_table_init(IPCNP_DEV);
	IRQ_CONNECT(IPC_NP_IRQ, INT_PRI_MIDDLE, IPC_INTHandler, (uint32_t)IPCNP_DEV, 0);
	irq_enable(IPC_NP_IRQ);

	return 0;
}
SYS_INIT(soc_ipc_irq_init, PRE_KERNEL_2, 0);
#else
BUILD_ASSERT(!IS_ENABLED(CONFIG_AS_INIC_NP),
	     "CONFIG_AS_INIC_NP needs the vendor IPC dispatcher, which the "
	     "rpmsg MBOX driver replaces on the shared NP IPC interrupt");
#endif
