/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ameba "image2 entry header" for the KM4 (Cortex-M55) non-secure application
 * image2.
 *
 * The vendor IMG1 bootloader does not use the Cortex-M reset vector to enter
 * image2: it reads the non-secure entry point from a RAM_START_FUNCTION
 * (Img2EntryFun0) struct that MUST occupy the very first bytes of the loaded
 * image2 payload, i.e. ORIGIN(RAM) == the sub-image load address (KM4_BD_DRAM
 * 0x60000020).  Without this struct the first words of the image are zero and
 * IMG1 jumps to a garbage address -> instant fault/boot-loop.
 *
 * This image is non-XIP (CONFIG_XIP=n): ROMABLE_REGION == RAMABLE_REGION == RAM,
 * so the linker's rom_start output section already starts at ORIGIN(RAM).  This
 * snippet is injected into rom_start via zephyr_linker_sources(ROM_START ...)
 * with a sort key ("0x0entry") that places it immediately after
 * arch/common/rom_start_offset.ld and immediately before the Cortex-M vector
 * table (sort key "0x0vectors").  The struct therefore lands at exactly
 * ORIGIN(RAM); the vector table's 0x200 VTOR alignment then zero-pads the rest
 * of the header gap, leaving the vectors at their usual offset.
 *
 * No flash->RAM copy is needed here (contrast amebag2's mcuboot path, which
 * copies .ram_image2.entry from its flash load address in soc_early_init_hook):
 * this image is loaded verbatim to RAM and is not an mcuboot image.
 */

	__image2_entry_func__ = .;
	KEEP(*(SORT(.image2.entry.data*)))
	__image2_entry_func_end__ = .;
