/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_AMEBASMART_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_AMEBASMART_PINCTRL_H_

/*
 * Function IDs mirror the AmebaSmart SDK's PINMUX_FUNCTION_* enum in
 * ameba-rtos/component/soc/amebasmart/fwlib/include/ameba_pinmux.h.
 */
#define AMEBA_GPIO           0
#define AMEBA_UART           1
#define AMEBA_LOG_UART       2
#define AMEBA_UART_RTSCTS    2
#define AMEBA_SPI            3
#define AMEBA_RTC            4
#define AMEBA_IR             5
#define AMEBA_SPIF           6
#define AMEBA_I2C            7
#define AMEBA_SDIOH          8
#define AMEBA_LEDC           9
#define AMEBA_PWM            10
#define AMEBA_SWD            11
#define AMEBA_AUDIO          12
#define AMEBA_I2S0           13
#define AMEBA_I2S1           13
#define AMEBA_I2S2           14
#define AMEBA_I2S3           15
#define AMEBA_SPK            16
#define AMEBA_AUXIN          16
#define AMEBA_DMIC           17
#define AMEBA_CAPTOUCH       18
#define AMEBA_SIC            19
#define AMEBA_MIPI           20
#define AMEBA_USB            21
#define AMEBA_FEM_C          22
#define AMEBA_ANT_SEL        22
#define AMEBA_EXT_ZIGBEE     23
#define AMEBA_BT_UART        24
#define AMEBA_BT_GPIO        25
#define AMEBA_BT_RF          26
#define AMEBA_DBG_BTCOEX_GNT 27
#define AMEBA_TIMER          28
#define AMEBA_DBGPORT        29
#define AMEBA_WAKEUP         30

/* Some peripherals reuse GPIO / ADC / TIMER function IDs upstream; provide
 * back-compat aliases used by the Zephyr Ameba driver code.
 */
#define AMEBA_ADC            AMEBA_GPIO

/*
 * Pin encoding: bit[14:13] port, bit[12:8] pin, bit[7:0] function ID.
 * AmebaSmart has three ports (A, B, C).
 */
#define AMEBA_PORT_PIN(port, line)       ((((port) - 'A') << 5) + (line))
#define AMEBA_PINMUX(port, line, funcid) (((AMEBA_PORT_PIN(port, line)) << 8) | (funcid))

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_AMEBASMART_PINCTRL_H_ */
