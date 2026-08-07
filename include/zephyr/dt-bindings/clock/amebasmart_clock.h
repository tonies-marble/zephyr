/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AMEBASMART_CLOCK_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AMEBASMART_CLOCK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Realtek AmebaSmart clock Devicetree bindings
 *
 * The RTL8730E consolidates all basic timers into a single TIM0..TIM13 range
 * (LP LTIM0..7 = TIM0..7, PWM = TIM8, PTIM = TIM9, HP HTIM10..13 = TIM10..13).
 * Some peripherals present on AmebaDPlus are absent on AmebaSmart (KSCAN,
 * PWM1, PTIM1, etc.); GDMA replaces DMAC and SDH replaces SDIO.
 *
 * Clock IDs (AMEBA_*_CLK) are kept compatible with the AmebaDPlus binding so
 * that driver code and devicetree entries can share generic names where the
 * peripheral maps to a valid AmebaSmart block.
 */

/**
 * @name AON domain clocks
 * @{
 */

/** ATIM clock in AON domain */
#define AMEBA_ATIM_CLK 1

/** RTC clock in AON domain */
#define AMEBA_RTC_CLK  2

/** @} */

/**
 * @name SYSON domain clocks
 * @{
 */

#define AMEBA_PWM0_CLK    3   /* TIM8 */
#define AMEBA_HTIM0_CLK   5   /* TIM10 */
#define AMEBA_HTIM1_CLK   6   /* TIM11 */
#define AMEBA_LEDC_CLK    7
#define AMEBA_UART0_CLK   8
#define AMEBA_UART1_CLK   9
#define AMEBA_UART2_CLK   10
#define AMEBA_LOGUART_CLK 11
#define AMEBA_DTIM_CLK    12
#define AMEBA_ADC_CLK     13
#define AMEBA_GPIO_CLK    14
#define AMEBA_LTIM0_CLK   15   /* TIM0 */
#define AMEBA_LTIM1_CLK   16   /* TIM1 */
#define AMEBA_LTIM2_CLK   17   /* TIM2 */
#define AMEBA_LTIM3_CLK   18   /* TIM3 */
#define AMEBA_LTIM4_CLK   19   /* TIM4 */
#define AMEBA_LTIM5_CLK   20   /* TIM5 */
#define AMEBA_LTIM6_CLK   21   /* TIM6 */
#define AMEBA_LTIM7_CLK   22   /* TIM7 */
#define AMEBA_PTIM0_CLK   23   /* TIM9 */

/** @} */

/**
 * @name SoC domain clocks
 * @{
 */

#define AMEBA_DMAC_CLK   26   /* GDMA */
#define AMEBA_SDIO_CLK   27   /* SDH */
#define AMEBA_SPI0_CLK   28
#define AMEBA_SPI1_CLK   29
#define AMEBA_USB_CLK    30
#define AMEBA_FLASH_CLK  31
#define AMEBA_PSRAM_CLK  32
#define AMEBA_SPORT0_CLK 33
#define AMEBA_SPORT1_CLK 34
#define AMEBA_AC_CLK     35
#define AMEBA_IRDA_CLK   36
#define AMEBA_I2C0_CLK   37
#define AMEBA_I2C1_CLK   38
#define AMEBA_TRNG_CLK   39

/** @} */

/**
 * @name Misc clocks
 * @{
 */

#define AMEBA_BTON_CLK 40

/**
 * @brief Maximum clock index (one past the last valid index).
 */
#define AMEBA_CLK_MAX 43 /* clk idx max */

/** @} */

/**
 * @name Peripheral clock helper macros
 * @{
 */

/**
 * @brief Define a clock entry using explicit APBPeriph symbols.
 *
 * AmebaSmart does not follow the AmebaDPlus naming for every peripheral
 * (e.g. GDMA vs DMAC, SDH vs SDIO, unified TIMx range), so entries are
 * generated with explicit APBPeriph mappings rather than via ##-concatenation.
 */
#define AMEBA_APB_PERIPH(clk_id, fen_sym, cke_sym)                                         \
	[clk_id] = {                                                                       \
		.parent = AMEBA_RCC_NO_PARENT,                                             \
		.cke = APBPeriph_##cke_sym##_CLOCK,                                        \
		.fen = APBPeriph_##fen_sym,                                                \
	},

/* LP LTIM0..LTIM7 map to TIM0..TIM7 */
#define AMEBA_LTIM_PERIPHS                                                                 \
	AMEBA_APB_PERIPH(AMEBA_LTIM0_CLK, TIM0, TIM0)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM1_CLK, TIM1, TIM1)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM2_CLK, TIM2, TIM2)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM3_CLK, TIM3, TIM3)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM4_CLK, TIM4, TIM4)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM5_CLK, TIM5, TIM5)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM6_CLK, TIM6, TIM6)                                      \
	AMEBA_APB_PERIPH(AMEBA_LTIM7_CLK, TIM7, TIM7)

/* PTIM0 -> TIM9 */
#define AMEBA_PTIM_PERIPHS AMEBA_APB_PERIPH(AMEBA_PTIM0_CLK, TIM9, TIM9)

/* SPI, SPORT, I2C, UART */
#define AMEBA_SPI_PERIPHS                                                                  \
	AMEBA_APB_PERIPH(AMEBA_SPI0_CLK, SPI0, SPI0)                                       \
	AMEBA_APB_PERIPH(AMEBA_SPI1_CLK, SPI1, SPI1)

#define AMEBA_SPORT_PERIPHS                                                                \
	AMEBA_APB_PERIPH(AMEBA_SPORT0_CLK, SPORT0, SPORT0)                                 \
	AMEBA_APB_PERIPH(AMEBA_SPORT1_CLK, SPORT1, SPORT1)

#define AMEBA_I2C_PERIPHS                                                                  \
	AMEBA_APB_PERIPH(AMEBA_I2C0_CLK, I2C0, I2C0)                                       \
	AMEBA_APB_PERIPH(AMEBA_I2C1_CLK, I2C1, I2C1)

#define AMEBA_UART_PERIPHS                                                                 \
	AMEBA_APB_PERIPH(AMEBA_UART0_CLK, UART0, UART0)                                    \
	AMEBA_APB_PERIPH(AMEBA_UART1_CLK, UART1, UART1)                                    \
	AMEBA_APB_PERIPH(AMEBA_UART2_CLK, UART2, UART2)

/* PWM0 -> TIM8. HTIM0/1 -> TIM10/11. */
#define AMEBA_PWM_PERIPHS  AMEBA_APB_PERIPH(AMEBA_PWM0_CLK, TIM8, TIM8)

#define AMEBA_HTIM_PERIPHS                                                                 \
	AMEBA_APB_PERIPH(AMEBA_HTIM0_CLK, TIM10, TIM10)                                    \
	AMEBA_APB_PERIPH(AMEBA_HTIM1_CLK, TIM11, TIM11)

/* Single-instance peripherals */
#define AMEBA_LOGUART_PERIPHS AMEBA_APB_PERIPH(AMEBA_LOGUART_CLK, LOGUART, LOGUART)
#define AMEBA_DMAC_PERIPHS    AMEBA_APB_PERIPH(AMEBA_DMAC_CLK,    GDMA,    GDMA)
#define AMEBA_SDIO_PERIPHS    AMEBA_APB_PERIPH(AMEBA_SDIO_CLK,    SDH,     SDH)
#define AMEBA_USB_PERIPHS     AMEBA_APB_PERIPH(AMEBA_USB_CLK,     USB,     USB)
#define AMEBA_FLASH_PERIPHS   AMEBA_APB_PERIPH(AMEBA_FLASH_CLK,   FLASH,   FLASH)
#define AMEBA_PSRAM_PERIPHS   AMEBA_APB_PERIPH(AMEBA_PSRAM_CLK,   PSRAM,   PSRAM)
#define AMEBA_AC_PERIPHS      AMEBA_APB_PERIPH(AMEBA_AC_CLK,      AC,      AC)
#define AMEBA_IRDA_PERIPHS    AMEBA_APB_PERIPH(AMEBA_IRDA_CLK,    IRDA,    IRDA)
#define AMEBA_TRNG_PERIPHS    AMEBA_APB_PERIPH(AMEBA_TRNG_CLK,    TRNG,    TRNG)
#define AMEBA_RTC_PERIPHS     AMEBA_APB_PERIPH(AMEBA_RTC_CLK,     RTC,     RTC)
#define AMEBA_LEDC_PERIPHS    AMEBA_APB_PERIPH(AMEBA_LEDC_CLK,    LEDC,    LEDC)
#define AMEBA_ADC_PERIPHS     AMEBA_APB_PERIPH(AMEBA_ADC_CLK,     ADC,     ADC)
#define AMEBA_GPIO_PERIPHS    AMEBA_APB_PERIPH(AMEBA_GPIO_CLK,    GPIO,    GPIO)
#define AMEBA_BTON_PERIPHS    AMEBA_APB_PERIPH(AMEBA_BTON_CLK,    BTON,    BTON)
#define AMEBA_KSCAN_PERIPHS   /* KSCAN not present on AmebaSmart */

/**
 * @brief Aggregated core peripheral clock mappings.
 */
#define AMEBA_CORE_PERIPHS                                                                 \
	AMEBA_RTC_PERIPHS                                                                  \
	AMEBA_PWM_PERIPHS                                                                  \
	AMEBA_HTIM_PERIPHS                                                                 \
	AMEBA_LEDC_PERIPHS                                                                 \
	AMEBA_UART_PERIPHS                                                                 \
	AMEBA_LOGUART_PERIPHS                                                              \
	AMEBA_ADC_PERIPHS                                                                  \
	AMEBA_GPIO_PERIPHS                                                                 \
	AMEBA_LTIM_PERIPHS                                                                 \
	AMEBA_PTIM_PERIPHS                                                                 \
	AMEBA_KSCAN_PERIPHS                                                                \
	AMEBA_DMAC_PERIPHS                                                                 \
	AMEBA_SDIO_PERIPHS                                                                 \
	AMEBA_SPI_PERIPHS                                                                  \
	AMEBA_USB_PERIPHS                                                                  \
	AMEBA_FLASH_PERIPHS                                                                \
	AMEBA_SPORT_PERIPHS                                                                \
	AMEBA_AC_PERIPHS                                                                   \
	AMEBA_I2C_PERIPHS                                                                  \
	AMEBA_TRNG_PERIPHS                                                                 \
	AMEBA_BTON_PERIPHS

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AMEBASMART_CLOCK_H_ */
