/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Skeleton SoC init for the AmebaSmart AP (Cortex-A32, "CA32") core.
 *
 * Zephyr on the AP is not yet functional; this stub exists so the port
 * shape mirrors imx93 (which supports both A-class and M-class targets
 * from a single SoC directory). The AP bring-up path — GIC init, MMU,
 * IPC handshake with the KM4 bootloader, RSIP configuration for the AP
 * XIP window at 0x0e000000 — will be filled in as the AP variant is
 * developed. Right now, building for rtl8730e/ap is expected to link
 * but not boot.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

void soc_early_init_hook(void)
{
	/* Placeholder — see file header. */
}
