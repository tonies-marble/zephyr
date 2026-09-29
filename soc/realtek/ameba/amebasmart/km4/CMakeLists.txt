# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# KM4 (Cortex-M55 / Armv8.1-M) Zephyr *application* (non-secure image2).
#
# This file is include()d from the amebasmart top-level CMakeLists.txt when
# CONFIG_SOC_RTL8730E_KM4 && !CONFIG_MCUBOOT (the MCUboot bootloader image for
# the same core takes the CONFIG_MCUBOOT branch there instead).  Because it is
# include()d (not add_subdirectory'd), CMAKE_CURRENT_SOURCE_DIR is still the
# amebasmart directory; use CMAKE_CURRENT_LIST_DIR for paths anchored here.

# The KM4 application is a standard Cortex-M image: XIP text/rodata in the KM4
# flash logic window, data/bss/stacks/heap in PSRAM (see rtl8730e_evb_km4.dts).
# The shared Cortex-M linker script handles both regions from the devicetree
# chosen zephyr,flash / zephyr,sram nodes.
set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../common/linker_cortex_m.ld CACHE INTERNAL "")

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

# Resolve the Real-M300V (KM4) ROM function entry points.  The KM4 mask ROM
# exposes a large non-secure API (LOGUART_*, DelayUs, CPU_ClkGet, FLASH_*,
# _strcmp, BKUP_*, ...) at fixed addresses; the HAL / lib_chipinfo.a and the
# Zephyr loguart driver call these.  ameba_rom_symbol_acut.ld PROVIDE()s their
# (non-secure ROM) addresses.  The HAL's amebasmart CMakeLists links this script
# for the MCUboot image only, so the KM4 application must pull it in here.
zephyr_link_libraries(
  -T${ZEPHYR_HAL_REALTEK_MODULE_DIR}/ameba/amebasmart/ld/ameba_rom_symbol_acut.ld)

# Ameba "image2 entry header": the vendor IMG1 reads the KM4 non-secure entry
# point from the RAM_START_FUNCTION (Img2EntryFun0) struct that must be the very
# first bytes of the loaded image2 (ORIGIN(RAM) = KM4_BD_DRAM 0x60000020).  This
# image is non-XIP, so rom_start already starts at ORIGIN(RAM); inject the entry
# struct into rom_start (ROM_START) with a sort key that lands right after
# arch/common/rom_start_offset.ld ("0x0") and before the Cortex-M vector table
# ("0x0vectors"), so Img2EntryFun0 sits at exactly ORIGIN(RAM).  See
# km4/image2_entry_section.ld and the Img2EntryFun0 definition in km4/soc.c.
zephyr_linker_sources(ROM_START SORT_KEY 0x0entry km4/image2_entry_section.ld)
# Build-time guard that the struct really landed at ORIGIN(RAM) (amebag2-style),
# injected at SECTIONS scope where a bare ASSERT is legal.
zephyr_linker_sources(SECTIONS km4/image2_entry_assert.ld)

# Home for the HAL's SRAM/DRAM-only code so it is not orphan-placed into the
# flash XIP region (see app_ram_section.ld).  Injected into RAM_SECTIONS so it
# lands in the RAMABLE_REGION with a flash load address.
zephyr_linker_sources(RAM_SECTIONS SORT_KEY 0x0 km4/app_ram_section.ld)

# Vendor IPC dispatcher registration table (see ipc_table_section.ld).
zephyr_linker_sources(DATA_SECTIONS SORT_KEY 0x1ipc ipc_table_section.ld)

zephyr_sources(${CMAKE_CURRENT_LIST_DIR}/soc.c)
