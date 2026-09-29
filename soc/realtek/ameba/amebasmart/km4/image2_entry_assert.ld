/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Build-time guard for the KM4 image2 entry header (see
 * km4/image2_entry_section.ld): the Img2EntryFun0 struct is only found by the
 * vendor IMG1 bootloader if it sits at the very first byte of the loaded image,
 * i.e. __image2_entry_func__ == ORIGIN(RAM) (KM4_BD_DRAM 0x60000020).  This
 * mirrors the amebag2 boot_section.ld ASSERT; it is injected at SECTIONS scope
 * (where a bare ASSERT is legal, unlike inside an output section) via
 * zephyr_linker_sources(SECTIONS ...), after __image2_entry_func__ is defined.
 */

ASSERT(__image2_entry_func__ == ORIGIN(RAM),
       "Img2EntryFun0 must sit at the start of RAM (image2 load address)")
