/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ameba "image2 entry header" for the KM0 (Cortex-M23) low-power core
 * application image2.
 *
 * The vendor IMG1 / LP bootloader does not use the Cortex-M reset vector to
 * enter image2: it reads the entry point from a RAM_START_FUNCTION
 * (Img2EntryFun0) struct that MUST occupy the very first bytes of the loaded
 * image2 payload, i.e. ORIGIN(RAM) == the sub-image load address (KM0_BD_RAM
 * 0x23002020).  Without this struct the first words of the image are zero and
 * the bootloader jumps to a garbage address.
 *
 * This image is non-XIP: ROMABLE_REGION == RAMABLE_REGION == RAM, so the
 * linker's rom_start output section already starts at ORIGIN(RAM).  This snippet
 * is injected into rom_start via zephyr_linker_sources(ROM_START ...) with a
 * sort key ("0x0entry") that places it immediately after
 * arch/common/rom_start_offset.ld and immediately before the Cortex-M vector
 * table (sort key "0x0vectors"), so the struct lands at exactly ORIGIN(RAM); the
 * vector table's alignment then zero-pads the rest of the header gap.
 *
 * The KM0 image is loaded verbatim to SRAM and is not an mcuboot image, so no
 * flash->RAM copy is needed.
 */

	__image2_entry_func__ = .;
	KEEP(*(SORT(.image2.entry.data*)))
	__image2_entry_func_end__ = .;
	/*
	 * Keep the pre-Zephyr entry trampoline (km0_boot_trampoline) here, in low SRAM
	 * immediately after the entry table, adjacent like the vendor entry stub at
	 * 0x23002060 (right after its own header block).  The ROM enters via a pointer
	 * in the table, so any placement is in principle reachable; keeping the
	 * trampoline next to the table is a conservative match to the vendor layout.
	 */
	. = ALIGN(4);
	KEEP(*(.image2.entry.text*))
