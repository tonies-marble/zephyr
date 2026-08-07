# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)

if(CONFIG_SOC_RTL8730E_HP)
  add_subdirectory(hp)
elseif(CONFIG_SOC_RTL8730E_AP)
  add_subdirectory(ap)
endif()
