# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SOC_RTL8730E_HP)
  dt_chosen(shelluart PROPERTY "zephyr,shell-uart")
  if(shelluart)
    dt_prop(shelluart_baudrate PATH ${shelluart} PROPERTY "current-speed")
    board_runner_args(amebaflash "--baudrate=${shelluart_baudrate}")
  endif()

  board_runner_args(amebaflash "--image-dir=${ZEPHYR_BINARY_DIR}/../images" "--device=${CONFIG_SOC_SERIES}")
  board_set_flasher_ifnset(amebaflash)
  board_finalize_runner_args(amebaflash)

  # JLink debug/attach for the KM4 (HP) core. The J-Link device profile is
  # "Cortex-M33" — matching what Realtek's own tooling uses in
  # ameba-rtos/tools/scripts/jlink_script/ameba_jlink_config.json5. Older
  # J-Link releases don't have a Cortex-M55 profile that reaches the KM4
  # correctly through the CoreSight fabric. The AP mapping (AP 1 for KM4)
  # is set in the AP1_KM4.JLinkScript.
  board_runner_args(jlink "--device=Cortex-M33" "--speed=4000")
  board_runner_args(jlink "--tool-opt=-JLinkScriptFile ${CMAKE_CURRENT_LIST_DIR}/support/AP1_KM4.JLinkScript")

  include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
endif()
