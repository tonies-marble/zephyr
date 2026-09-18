/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for Realtek Ameba Led Controller
 */

#define DT_DRV_COMPAT realtek_ameba_ledc

/* Include <soc.h> before <ameba_soc.h> to avoid redefining unlikely() macro */
#include <soc.h>
#include <ameba_soc.h>

#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/led/led.h>

#include <zephyr/kernel.h>

#ifdef CONFIG_LEDC_AMEBA_DMA
#include <zephyr/drivers/dma.h>
#include <zephyr/cache.h>
#include "dma_ameba_gdma.h"
#endif

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ledc_ameba);

#define RESULT_RUNNING  0
#define RESULT_COMPLETE 1
#define RESULT_ERR      2

#define LEDC_TX_TIMEOUT K_MSEC(1000)

/* One packed word per pixel */
#define LEDC_TX_BUF_WORDS DT_INST_PROP(0, chain_length)

#ifdef CONFIG_LEDC_AMEBA_DMA
#define LEDC_TX_BUF_ALIGN CONFIG_DCACHE_LINE_SIZE
#else
#define LEDC_TX_BUF_ALIGN sizeof(uint32_t)
#endif

static uint32_t ledc_tx_buf[LEDC_TX_BUF_WORDS] __aligned(LEDC_TX_BUF_ALIGN);

struct ameba_ledc_data_struct {
	LEDC_InitTypeDef ledc_init_struct;

	uint32_t *tx_data;     /* tx data handle (points at ledc_tx_buf) */
	uint16_t tx_total_len; /* tx total length */
	uint16_t tx_len;       /* tx len that has been wrote to the FIFO */
	uint8_t irq_result;    /* tx status, published to the caller via tx_done_sem */
	struct k_sem tx_done_sem;

#ifdef CONFIG_LEDC_AMEBA_DMA
	const struct device *dma_dev;
	uint32_t dma_channel;
	struct dma_config dma_cfg;
	struct dma_block_config blk_cfg;
#endif
};

struct ameba_ledc_cfg_struct {
	const struct pinctrl_dev_config *pinctrl_dev;
	const struct device *clock_dev;
	const uint8_t *color_mapping;
	const clock_control_subsys_t clock_subsys;

	/* Raw LEDC register counts of 25 ns each, not nanoseconds despite the name */
	uint32_t wait_data_time_ns;
	uint32_t reset_ns;

	uint32_t t0h_ns;
	uint32_t t0l_ns;
	uint32_t t1h_ns;
	uint32_t t1l_ns;

	uint16_t led_num_cfg; /* max 1024 */

	uint8_t num_colors;
	uint8_t output_rgb_mode;
};

static void ameba_ledc_isr_handle(const struct device *dev)
{
	struct ameba_ledc_data_struct *pdata = dev->data;
	uint32_t intr_status;

	LEDC_INTConfig(LEDC_DEV, LEDC_BIT_GLOBAL_INT_EN, DISABLE);

	intr_status = LEDC_GetINT(LEDC_DEV);

#ifndef CONFIG_LEDC_AMEBA_DMA
	if (intr_status & LEDC_BIT_FIFO_CPUREQ_INT) {
		uint32_t ledc_fifothr;
		uint32_t *start_addr;

		LEDC_ClearINT(LEDC_DEV, LEDC_BIT_FIFO_CPUREQ_INT);

		ledc_fifothr = LEDC_GetFIFOLevel(LEDC_DEV);
		start_addr = pdata->tx_data + pdata->tx_len;

		if ((pdata->tx_total_len - pdata->tx_len) >= ledc_fifothr) {
			pdata->tx_len += LEDC_SendData(LEDC_DEV, start_addr, ledc_fifothr);
		} else {
			pdata->tx_len += LEDC_SendData(LEDC_DEV, start_addr,
						       pdata->tx_total_len - pdata->tx_len);
		}

		LEDC_INTConfig(LEDC_DEV, LEDC_BIT_GLOBAL_INT_EN, ENABLE);
		return;
	}
#endif /* !CONFIG_LEDC_AMEBA_DMA */

	if (intr_status & LEDC_BIT_LED_TRANS_FINISH_INT) {
		LEDC_ClearINT(LEDC_DEV, LEDC_BIT_LED_TRANS_FINISH_INT);

		pdata->irq_result = RESULT_COMPLETE;
		LEDC_SoftReset(LEDC_DEV);
	}

	if (intr_status & LEDC_BIT_WAITDATA_TIMEOUT_INT) {
		LEDC_ClearINT(LEDC_DEV, LEDC_BIT_WAITDATA_TIMEOUT_INT);

		pdata->irq_result = RESULT_ERR;
		LEDC_SoftReset(LEDC_DEV);
	}

	if (intr_status & LEDC_BIT_FIFO_OVERFLOW_INT) {
		LEDC_ClearINT(LEDC_DEV, LEDC_BIT_FIFO_OVERFLOW_INT);

		pdata->irq_result = RESULT_ERR;
		LEDC_SoftReset(LEDC_DEV);
	}

	if (pdata->irq_result != RESULT_RUNNING) {
		k_sem_give(&pdata->tx_done_sem);
	}

	LEDC_INTConfig(LEDC_DEV, LEDC_BIT_GLOBAL_INT_EN, ENABLE);
}

#ifdef CONFIG_LEDC_AMEBA_DMA
static void ameba_ledc_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel,
				    int status)
{
	const struct device *dev = arg;
	struct ameba_ledc_data_struct *pdata = dev->data;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if (status < 0) {
		LOG_ERR("Ledc DMA error %d", status);
		pdata->irq_result = RESULT_ERR;
		k_sem_give(&pdata->tx_done_sem);
	}

	/* Completion is handled by the LEDC LED_TRANS_FINISH interrupt */
}

static int ameba_ledc_dma_start(const struct device *dev, uint16_t data_len)
{
	struct ameba_ledc_data_struct *pdata = dev->data;
	int ret;

	pdata->dma_cfg.head_block = &pdata->blk_cfg;
	pdata->dma_cfg.user_data = (void *)dev;

	pdata->blk_cfg.source_address = (uint32_t)pdata->tx_data;
	pdata->blk_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	pdata->blk_cfg.dest_address = (uint32_t)&LEDC_DEV->LEDC_DATA_REG;
	pdata->blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	pdata->blk_cfg.block_size = (uint32_t)data_len * sizeof(uint32_t);

	ret = dma_config(pdata->dma_dev, pdata->dma_channel, &pdata->dma_cfg);
	if (ret < 0) {
		LOG_ERR("dma_config failed %d", ret);
		return ret;
	}

	/* Clean D-cache before the DMA reads the buffer */
	sys_cache_data_flush_range(pdata->tx_data, pdata->blk_cfg.block_size);

	ret = dma_start(pdata->dma_dev, pdata->dma_channel);
	if (ret < 0) {
		LOG_ERR("dma_start failed %d", ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_LEDC_AMEBA_DMA */

static int ameba_ledc_update_rgb(const struct device *dev, struct led_rgb *pixels,
				 size_t num_pixels)
{
	const struct ameba_ledc_cfg_struct *cfg = dev->config;
	struct ameba_ledc_data_struct *pdata = dev->data;
	uint16_t data_len = (uint16_t)num_pixels;
	uint16_t i;
	int ret = 0;

	/* LEDC_MAX_DATA_LENGTH 0x2000 */
	if (!IS_LEDC_DATA_LENGTH(num_pixels)) {
		LOG_WRN("Total data length too long, force to Max %d", LEDC_MAX_DATA_LENGTH);
		data_len = LEDC_MAX_DATA_LENGTH;
	}

	/* Internal buffer is sized to the configured chain length */
	if (data_len > LEDC_TX_BUF_WORDS) {
		LOG_WRN("num_pixels %u > chain-length %u, truncating", data_len,
			(unsigned int)LEDC_TX_BUF_WORDS);
		data_len = LEDC_TX_BUF_WORDS;
	}

	pdata->tx_len = 0;
	pdata->tx_data = ledc_tx_buf;
	pdata->tx_total_len = data_len;
	pdata->irq_result = RESULT_RUNNING;
	k_sem_reset(&pdata->tx_done_sem);

	pdata->ledc_init_struct.data_length = data_len;
	LEDC_SetTotalLength(LEDC_DEV, pdata->ledc_init_struct.data_length);

	/* Pack color_mapping[0] into the top used byte (sent first, MSB-first) */
	for (i = 0; i < data_len; i++) {
		uint32_t word = 0;
		uint8_t shift = (cfg->num_colors - 1) * 8;
		uint8_t j;

		for (j = 0; j < cfg->num_colors; j++) {
			uint8_t val;

			switch (cfg->color_mapping[j]) {
			case LED_COLOR_ID_RED:
				val = pixels[i].r;
				break;
			case LED_COLOR_ID_GREEN:
				val = pixels[i].g;
				break;
			case LED_COLOR_ID_BLUE:
				val = pixels[i].b;
				break;
			default:
				return -EINVAL;
			}

			word |= (uint32_t)val << shift;
			shift -= 8;
		}

		ledc_tx_buf[i] = word;
	}

	LOG_DBG("Write %d data 0x%08x cnt %d", data_len, ledc_tx_buf[0], cfg->num_colors);

#ifdef CONFIG_LEDC_AMEBA_DMA
	/* Arm DMA before enabling LEDC */
	ret = ameba_ledc_dma_start(dev, data_len);
	if (ret < 0) {
		return ret;
	}
#endif

	LEDC_Cmd(LEDC_DEV, ENABLE);

	if (k_sem_take(&pdata->tx_done_sem, LEDC_TX_TIMEOUT) != 0) {
		LOG_WRN("Ledc TX timeout");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (pdata->irq_result == RESULT_COMPLETE) {
		LOG_DBG("Ledc TX done!");
		ret = 0;
		goto out;
	}

	LOG_WRN("Ledc exit %d", pdata->irq_result);
	ret = -EFAULT;

out:
#ifdef CONFIG_LEDC_AMEBA_DMA
	dma_stop(pdata->dma_dev, pdata->dma_channel);
#endif
	return ret;
}

static int ameba_ledc_update_channels(const struct device *dev, uint8_t *channels,
				      size_t num_channels)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channels);
	ARG_UNUSED(num_channels);
	LOG_WRN("Update_channels not support");
	return -ENOTSUP;
}

static DEVICE_API(led_strip, ameba_ledc_api) = {
	.update_rgb = ameba_ledc_update_rgb,
	.update_channels = ameba_ledc_update_channels,
};

static int ameba_ledc_init(const struct device *dev)
{
	const struct ameba_ledc_cfg_struct *cfg = dev->config;
	struct ameba_ledc_data_struct *data = dev->data;
	LEDC_InitTypeDef *pledc_init_struct = &(data->ledc_init_struct);
	uint16_t led_num;
	int err = 0;

	k_sem_init(&data->tx_done_sem, 0, 1);

	/* enable clock */
	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("Clock control device not ready");
		return -ENODEV;
	}

	err = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	if (err < 0 && err != -EALREADY) {
		LOG_ERR("Enable clk %d err %d", (uint32_t)cfg->clock_subsys, err);
		return err;
	}

#ifdef CONFIG_LEDC_AMEBA_DMA
	if (!device_is_ready(data->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}
#endif

	/* enable pinctrl */
	if (pinctrl_apply_state(cfg->pinctrl_dev, PINCTRL_STATE_DEFAULT)) {
		LOG_ERR("Pinctrl device not ready");
		return -ENODEV;
	}

	/* check the dts config valid
	 * LEDC_MAX_LED_NUM 1024
	 */
	if (!IS_LEDC_LED_NUM(cfg->led_num_cfg)) {
		LOG_ERR("Illegal parameter: LED cnt %d, force to Max %d", cfg->led_num_cfg,
			LEDC_MAX_LED_NUM);
		led_num = LEDC_MAX_LED_NUM;
	} else {
		led_num = cfg->led_num_cfg;
	}

	/* ledc init */
	LEDC_StructInit(pledc_init_struct);

	pledc_init_struct->led_count = led_num;
#ifdef CONFIG_LEDC_AMEBA_DMA
	pledc_init_struct->ledc_trans_mode = LEDC_DMA_MODE;
#else
	pledc_init_struct->ledc_trans_mode = LEDC_CPU_MODE;
#endif
	pledc_init_struct->t1h_ns = cfg->t1h_ns;
	pledc_init_struct->t1l_ns = cfg->t1l_ns;
	pledc_init_struct->t0h_ns = cfg->t0h_ns;
	pledc_init_struct->t0l_ns = cfg->t0l_ns;
	pledc_init_struct->reset_ns = cfg->reset_ns;
	pledc_init_struct->wait_data_time_ns = cfg->wait_data_time_ns;
	pledc_init_struct->output_RGB_mode = cfg->output_rgb_mode;
	pledc_init_struct->data_length = LEDC_DEFAULT_LED_NUM;
	pledc_init_struct->ledc_fifo_level = 0xF;
	pledc_init_struct->ledc_polarity = LEDC_IDLE_POLARITY_LOW;
	pledc_init_struct->wait_time0_en = ENABLE;
	pledc_init_struct->wait_time1_en = ENABLE;
	/* Register counts of 25 ns each, WAIT_TIME = 25 ns * (count + 1) */
	pledc_init_struct->wait_time0_ns = 0xEF;      /* ~6 us */
	pledc_init_struct->wait_time1_ns = 0x2625A00; /* ~1 s */

	LEDC_Init(LEDC_DEV, pledc_init_struct);

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), ameba_ledc_isr_handle,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	LOG_DBG("Ledc init finish");

	return 0;
}

PINCTRL_DT_INST_DEFINE(0);
static const uint8_t ameba_ledc_color_mapping[] = DT_INST_PROP(0, color_mapping);
static struct ameba_ledc_data_struct ameba_ledc_data = {
#ifdef CONFIG_LEDC_AMEBA_DMA
	.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(0, tx)),
	.dma_channel = DT_INST_DMAS_CELL_BY_NAME(0, tx, channel),
	.dma_cfg = AMEBA_DMA_CONFIG(0, tx, 1, ameba_ledc_dma_callback),
#endif
};
static const struct ameba_ledc_cfg_struct ameba_ledc_cfg = {
	.pinctrl_dev = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, idx),
	.num_colors = DT_INST_PROP_LEN(0, color_mapping),
	.color_mapping = ameba_ledc_color_mapping,

	.led_num_cfg = DT_INST_PROP(0, chain_length),
	.output_rgb_mode = DT_INST_PROP(0, output_rgb_mode),
	.wait_data_time_ns = DT_INST_PROP(0, wait_data_timeout),
	.t0h_ns = DT_INST_PROP(0, data_tx_time0h),
	.t0l_ns = DT_INST_PROP(0, data_tx_time0l),
	.t1h_ns = DT_INST_PROP(0, data_tx_time1h),
	.t1l_ns = DT_INST_PROP(0, data_tx_time1l),
	.reset_ns = DT_INST_PROP(0, refresh_time),
};

DEVICE_DT_INST_DEFINE(0, &ameba_ledc_init, NULL, &ameba_ledc_data, &ameba_ledc_cfg, POST_KERNEL,
		      CONFIG_LED_STRIP_INIT_PRIORITY, &ameba_ledc_api);
