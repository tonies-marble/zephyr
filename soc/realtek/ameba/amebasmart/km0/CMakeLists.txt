# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# KM0 Cortex-M23 low-power core application (image2).
#
# The KM0 image runs from the LS-domain SRAM (KM0_BD_RAM) with its .text/.data
# copied there by the LP bootloader.  We reuse the shared Cortex-M linker
# template; the KM0 LS-domain memory regions (KM0_BD_RAM main SRAM +
# KM0_IMG2_XIP flash window, per project_lp ameba_layout.ld) are supplied
# through the KM0 devicetree (zephyr,sram / zephyr,flash chosen nodes), so the
# generic template resolves the correct ORIGIN/LENGTH for this core.

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../common/linker_cortex_m.ld CACHE INTERNAL "")

# Ameba "image2 entry header": the vendor IMG1 / LP bootloader reads the KM0
# entry point from the RAM_START_FUNCTION (Img2EntryFun0) struct that must be the
# very first bytes of the loaded image2 (ORIGIN(RAM) = KM0_BD_RAM 0x23002020).
# This image is non-XIP, so rom_start already starts at ORIGIN(RAM); inject the
# entry struct into rom_start (ROM_START) with a sort key that lands right after
# arch/common/rom_start_offset.ld ("0x0") and before the Cortex-M vector table
# ("0x0vectors"), so Img2EntryFun0 sits at exactly ORIGIN(RAM).  See
# km0/image2_entry_section.ld and the Img2EntryFun0 definition in
# km0/system_ameba_lp.c.
zephyr_linker_sources(ROM_START SORT_KEY 0x0entry km0/image2_entry_section.ld)
# Build-time guard that the struct really landed at ORIGIN(RAM) (amebag2-style),
# injected at SECTIONS scope where a bare ASSERT is legal.
zephyr_linker_sources(SECTIONS km0/image2_entry_assert.ld)

# Minimal KM0 system source: SystemCoreClock for the LP core.  The SDK's
# ram_lp/ameba_system.c is only compiled for the CA32/HP targets, so KM0
# provides its own definition here.
zephyr_sources(${CMAKE_CURRENT_LIST_DIR}/system_ameba_lp.c)

# DiagVprintf for the HAL logging path (no KM0-ABI chipinfo archive shipped).
zephyr_sources(${CMAKE_CURRENT_LIST_DIR}/rom_patch.c)

# Vendor IPC dispatcher registration table (see ipc_table_section.ld).
zephyr_linker_sources(DATA_SECTIONS SORT_KEY 0x1ipc ipc_table_section.ld)

# The KM0 boot ROM (Cortex-M23) exports helper routines (LOGUART_*, DelayUs,
# BKUP_*, _strcmp, ...) at fixed addresses that the Realtek HAL and the Zephyr
# loguart driver call.  ameba_rom_symbol_km0_acut.ld PROVIDE()s those symbols at
# the KM0 ROM addresses (project_lp), mirroring how the KM4 app consumes its own
# ameba_rom_symbol_acut.ld.  KM0 has no chipinfo archive, so this script is the
# sole provider of the ROM symbols.
zephyr_link_libraries(
  -T${ZEPHYR_HAL_REALTEK_MODULE_DIR}/ameba/amebasmart/ld/ameba_rom_symbol_km0_acut.ld)
