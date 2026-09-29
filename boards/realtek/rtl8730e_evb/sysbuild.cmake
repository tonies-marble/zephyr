# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# Board-level sysbuild hook for RTL8730E EVB.
# Included by images/boards/CMakeLists.txt AFTER ExternalZephyrProject_Add()
# for all images but BEFORE ExternalZephyrProject_Cmake() is called.
#
# The RTL8730E is a tri-core part. A full image set is:
#   * mcuboot        - bootloader, runs on the KM4 (Cortex-M55) core
#   * <default image>- the application the user selected (usually the CA32 app)
#   * rtl8730e_km4   - the KM4 network-processor application (optional second
#                      binary, added when SB_CONFIG_RTL8730E_BUILD_KM4_APP=y)
#   * rtl8730e_km0   - the KM0 low-power application (optional third binary,
#                      added when SB_CONFIG_RTL8730E_BUILD_KM0_APP=y)
#
# In the vendor-IMG1 bring-up model (no MCUboot; default image is the CA32 app)
# the CA32 image-assembly step (merge_bin.py handle_amebasmart) folds the KM4
# and KM0 packaged image2 sub-images into a single app.bin that is flashed at
# the vendor IMG2 slot alongside the unchanged vendor boot.bin. The KM4/KM0 app
# images are therefore build-only helpers whose outputs the CA32 image consumes;
# they must build BEFORE the CA32 image so their images/km*_image2_all.bin exist
# when handle_amebasmart runs.
#
# MCUboot is always pinned to the KM4 cluster regardless of which cluster the
# default image targets.
if(TARGET mcuboot)
  set_target_properties(mcuboot PROPERTIES BOARD rtl8730e_evb/rtl8730e/km4)

  # The KM4 MCUboot Kconfig fragment and devicetree overlay live in the board
  # tree (mcuboot/km4.{conf,overlay}) rather than in the MCUboot repo, so that
  # repo stays pristine.  Inject them into the mcuboot image build here.  These
  # cache vars are snapshotted by sysbuild_cache(CREATE) at
  # ExternalZephyrProject_Cmake() time -- after this board hook runs -- so
  # setting them here reaches the child mcuboot build.  Respect a user-provided
  # app-local sysbuild/mcuboot.{conf,overlay} if present (do not clobber it).
  #
  # Conf uses EXTRA_CONF_FILE (additive): the fragment layers on top of
  # MCUboot's prj.conf, exactly as the previous in-tree board conf did (which
  # Zephyr appended to CONF_FILE).  The merged .config is identical either way.
  #
  # Overlay uses DTC_OVERLAY_FILE (REPLACING), not EXTRA_DTC_OVERLAY_FILE
  # (additive), and this asymmetry is deliberate.  When the overlay lived in the
  # MCUboot repo (boot/zephyr/boards/rtl8730e_evb_rtl8730e_km4.overlay), Zephyr's
  # board-overlay auto-discovery set DTC_OVERLAY_FILE to it, which REPLACES the
  # default overlay list -- so MCUboot's own app.overlay (it sets
  # zephyr,code-partition = &boot_partition) was never applied, and the board
  # km4.dts's zephyr,code-partition = &slot0_partition stayed in effect
  # (FLASH_LOAD_OFFSET = 0x40000).  Injecting the relocated overlay additively
  # would instead KEEP app.overlay, flip code-partition to &boot_partition, and
  # change FLASH_LOAD_OFFSET to 0x0 -- a silent change to the built MCUboot
  # image.  Replacing reproduces the original overlay set exactly, so the
  # relocation is a pure move with a byte-identical mcuboot artifact.
  if(NOT mcuboot_EXTRA_CONF_FILE)
    set(mcuboot_EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/mcuboot/km4.conf
        CACHE INTERNAL "RTL8730E KM4 MCUboot Kconfig fragment")
  endif()
  if(NOT mcuboot_DTC_OVERLAY_FILE)
    set(mcuboot_DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/mcuboot/km4.overlay
        CACHE INTERNAL "RTL8730E KM4 MCUboot devicetree overlay")
  endif()
endif()

# Resolve an app source dir that may be given relative to APP_DIR (per the
# Kconfig help), returning an absolute path in ${out_var}.
function(rtl8730e_resolve_app_dir out_var src)
  if(IS_ABSOLUTE "${src}")
    set(${out_var} "${src}" PARENT_SCOPE)
  else()
    get_filename_component(_abs "${APP_DIR}/${src}" ABSOLUTE)
    set(${out_var} "${_abs}" PARENT_SCOPE)
  endif()
endfunction()

# Optionally build the KM4 application as a second image alongside the default
# (CA32) application, so a single `west build` produces both binaries.
if(SB_CONFIG_RTL8730E_BUILD_KM4_APP AND NOT TARGET rtl8730e_km4)
  rtl8730e_resolve_app_dir(_km4_src "${SB_CONFIG_RTL8730E_KM4_APP_SOURCE_DIR}")
  ExternalZephyrProject_Add(
    APPLICATION rtl8730e_km4
    SOURCE_DIR  ${_km4_src}
    BOARD       rtl8730e_evb/rtl8730e/km4
  )
endif()

# Optionally build the KM0 application as a third image.
if(SB_CONFIG_RTL8730E_BUILD_KM0_APP AND NOT TARGET rtl8730e_km0)
  rtl8730e_resolve_app_dir(_km0_src "${SB_CONFIG_RTL8730E_KM0_APP_SOURCE_DIR}")
  ExternalZephyrProject_Add(
    APPLICATION rtl8730e_km0
    SOURCE_DIR  ${_km0_src}
    BOARD       rtl8730e_evb/rtl8730e/km0
  )
endif()

# When the default image is the CA32 combined-app (no MCUboot), the KM4/KM0 app
# images are build-only inputs to it: their image2 outputs are merged into the
# CA32 app.bin. Mark them build-only (so they are not independently flashed) and
# order them before the CA32 image so their outputs exist at CA32 assembly time.
if(NOT SB_CONFIG_BOOTLOADER_MCUBOOT AND DEFINED DEFAULT_IMAGE)
  foreach(core_img rtl8730e_km4 rtl8730e_km0)
    if(TARGET ${core_img})
      set_target_properties(${core_img} PROPERTIES BUILD_ONLY True)
      # Build-order dependency (ExternalProject targets): CA32 builds last.
      add_dependencies(${DEFAULT_IMAGE} ${core_img})
      # Configure-order dependency (ExternalZephyrProject_Cmake ordering).
      sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} ${core_img})
    endif()
  endforeach()
endif()
