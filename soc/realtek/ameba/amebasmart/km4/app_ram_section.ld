/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * KM4 application: give the Realtek "SRAM/DRAM-only" code a home in RAM.
 *
 * lib_chipinfo.a (ameba_rom_patch.o) and other HAL objects emit code into
 * SRAMDRAM_ONLY_TEXT_SECTION (".sramdram.only.text*") / SRAM_ONLY_TEXT_SECTION
 * (".sram.only.text*"): routines (e.g. flash-clock / flash-write helpers) that
 * must NOT execute from the flash XIP window.  Without an explicit rule the
 * linker orphan-places them (and, worse, could leave them in the XIP region).
 *
 * This image is XIP (ROMABLE_REGION = FLASH), so place these sections in RAM
 * with a flash load address (GROUP_DATA_LINK_IN): they are copied to PSRAM
 * alongside .data at startup and execute from there.
 */

SECTION_PROLOGUE(.ram_image2_text,,)
{
	. = ALIGN(32);
	*(.sramdram.only.text*)
	*(.sram.only.text*)
	*(.non.dram.text*)
	*(.sramdram.only.data*)
	. = ALIGN(32);
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

/* Vendor aliases for the non-cacheable window: the WiFi NP libraries'
 * .nocache.data inputs land in Zephyr's CONFIG_NOCACHE_MEMORY region
 * (arch/common/nocache.ld collects ".nocache.*"; the MPU marks it NC).
 */
#if defined(CONFIG_NOCACHE_MEMORY)
PROVIDE(__ram_nocache_start__ = _nocache_ram_start);
PROVIDE(__ram_nocache_end__ = _nocache_ram_end);
#endif
