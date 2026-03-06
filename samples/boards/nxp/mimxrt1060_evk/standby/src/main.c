/*
 * Copyright (c) 2022 Whisper.ai
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>

#define BUSY_WAIT_S 2U
#define SLEEP_S     2U
#define SOFT_OFF_S  10U

#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});

// #include "fsl_common.h"
// #include "fsl_iomuxc.h"
// #include "fsl_gpio.h"

// static void set_pmic_stby_req(int val) {
//   /* GPIO configuration of PERI_PWREN on PMIC_STBY_REQ (pin L7) */
//   gpio_pin_config_t PERI_PWREN_config = {
//       .direction = kGPIO_DigitalOutput,
//       .outputLogic = val,
//       .interruptMode = kGPIO_NoIntmode
//   };
//   /* Initialize GPIO functionality on PMIC_STBY_REQ (pin L7) */
//   GPIO_PinInit(GPIO5, 2U, &PERI_PWREN_config);

//   IOMUXC_SetPinMux(IOMUXC_SNVS_PMIC_STBY_REQ_GPIO5_IO02, 0U);
//   IOMUXC_SetPinConfig(IOMUXC_SNVS_PMIC_STBY_REQ_GPIO5_IO02, 0x10B0U);
// }

int main(void)
{
	printk("\n%s system off demo\n", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n", button.port->name);
		return 0;
	}

	/* Configure to generate PORT event (wakeup) on button press. */
	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n", ret, button.port->name,
		       button.pin);
		return 0;
	}
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
		       button.port->name, button.pin);
		return 0;
	}
	NXP_ENABLE_WAKEUP_SIGNAL(GPIO5_Combined_0_15_IRQn);

	printk("Busy-wait %u s\n", BUSY_WAIT_S);
	k_busy_wait(BUSY_WAIT_S * USEC_PER_SEC);

	printk("Sleep %u s\n", SLEEP_S);
	k_sleep(K_SECONDS(SLEEP_S));

	printk("Entering standby; press %s to restart sooner\n", button.port->name);
	pm_state_force(0u, &(struct pm_state_info){PM_STATE_STANDBY, 0, 0});
	/* Now we need to go sleep. This will let the idle thread run and
	 * the pm subsystem will use the forced state.
	 */
	k_sleep(K_SECONDS(SOFT_OFF_S));

	printk("ERROR: Standby failed\n");
	while (true) {
		/* spin to avoid fall-off behavior */
	}
	return 0;
}
