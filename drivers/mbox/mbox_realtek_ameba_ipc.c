/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX driver for the Realtek RTL8730E (AmebaSmart) inter-processor
 * communication (IPC) doorbell.
 *
 * Each CPU (CA32/AP, KM4/NP, KM0/LP) owns a 5 x u32 IPC register block.  This
 * driver models one block as an MBOX controller whose single mbox cell is the
 * ABSOLUTE hardware register bit index 0..31 (#mbox-cells = 1):
 *
 *   TX_DATA doorbell : TX1 group = bits  0..7, TX0 group = bits  8..15
 *   ISR/IMR RX-full  : RX1 group = bits 16..23, RX0 group = bits 24..31
 *
 * This deliberately mirrors the register layout (ameba_ipc.h) so the DT node
 * carries the full per-link bit-math and the driver stays link-agnostic:
 *   - TX: write BIT(cell) into this core's TX_DATA.  The hardware mirrors it
 *     into the peer's RX_DATA and raises the peer's RX-full status.
 *   - RX: the incoming signal appears as ISR bit `cell`, is masked/unmasked via
 *     IMR bit `cell` and acknowledged by writing that bit back to ISR (W1C).
 *
 * The group shift is direction-dependent in the vendor HAL
 * (ameba_ipc_api.c: ipc_table_init / ipc_send_message), so encoding the
 * absolute bit in DT is what lets one driver serve every core pair:
 *
 *   link / direction   TX bit (sender)   RX-full bit (receiver)
 *   AP_TO_NP (CA32->KM4)  ch+0               ch+16
 *   NP_TO_AP (KM4->CA32)  ch+0               ch+16
 *   AP_TO_LP (CA32->KM0)  ch+8               ch+16
 *   LP_TO_AP (KM0->CA32)  ch+0               ch+24
 *
 * On the CA32 the single IPCAP block (and its one interrupt) is shared by BOTH
 * the KM4 and KM0 rpmsg instances: they register RX callbacks on disjoint bits
 * (KM4 rx = bit 18, KM0 rx = bit 28) of the same device.
 *
 * A single, level-triggered interrupt line per core demuxes all bits; the
 * standard Zephyr IRQ_CONNECT plumbing abstracts GIC (CA32) vs NVIC (KM4/KM0).
 */

#define DT_DRV_COMPAT realtek_rtl8730e_ipc

#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mbox_realtek_ameba_ipc, CONFIG_MBOX_LOG_LEVEL);

/* Register offsets (bytes) within an IPC block. */
#define IPC_REG_TX_DATA 0x00
#define IPC_REG_RX_DATA 0x04
#define IPC_REG_ISR     0x08
#define IPC_REG_IMR     0x0C
#define IPC_REG_ICR     0x10

/*
 * The mbox "channel" cell is the absolute register bit index, so there are 32
 * of them (one per bit of TX_DATA / ISR / IMR).
 */
#define IPC_NUM_BITS 32

struct mbox_realtek_ipc_config {
	uintptr_t base;
	void (*irq_config_func)(void);
};

struct mbox_realtek_ipc_data {
	mbox_callback_t cb[IPC_NUM_BITS];
	void *user_data[IPC_NUM_BITS];
};

static inline uint32_t ipc_read(const struct mbox_realtek_ipc_config *cfg, uint32_t off)
{
	return sys_read32(cfg->base + off);
}

static inline void ipc_write(const struct mbox_realtek_ipc_config *cfg, uint32_t off, uint32_t val)
{
	sys_write32(val, cfg->base + off);
}

static void mbox_realtek_ipc_isr(const struct device *dev)
{
	const struct mbox_realtek_ipc_config *cfg = dev->config;
	struct mbox_realtek_ipc_data *data = dev->data;
	uint32_t status;

	/* Only act on events that are also unmasked. */
	uint32_t raw = ipc_read(cfg, IPC_REG_ISR);

	status = raw & ipc_read(cfg, IPC_REG_IMR);

	for (uint32_t bit = 0; bit < IPC_NUM_BITS; bit++) {
		if ((status & BIT(bit)) == 0U) {
			continue;
		}

		if (data->cb[bit] != NULL) {
			data->cb[bit](dev, bit, data->user_data[bit], NULL);
		}

		/* Acknowledge (write-1-to-clear). */
		ipc_write(cfg, IPC_REG_ISR, BIT(bit));
	}
}

static int mbox_realtek_ipc_send(const struct device *dev, uint32_t channel,
				 const struct mbox_msg *msg)
{
	const struct mbox_realtek_ipc_config *cfg = dev->config;

	if (channel >= IPC_NUM_BITS) {
		return -EINVAL;
	}

	/* Signalling only: no payload is transferred through the doorbell. */
	if (msg != NULL) {
		return -ENOTSUP;
	}

	/* Ring the peer's doorbell: `channel` is the absolute TX_DATA bit. */
	ipc_write(cfg, IPC_REG_TX_DATA, BIT(channel));

	return 0;
}

static int mbox_realtek_ipc_register_callback(const struct device *dev, uint32_t channel,
					      mbox_callback_t cb, void *user_data)
{
	struct mbox_realtek_ipc_data *data = dev->data;

	if (channel >= IPC_NUM_BITS) {
		return -EINVAL;
	}

	data->cb[channel] = cb;
	data->user_data[channel] = user_data;

	return 0;
}

static int mbox_realtek_ipc_mtu_get(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Doorbell only: no in-band data. */
	return 0;
}

static uint32_t mbox_realtek_ipc_max_channels_get(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IPC_NUM_BITS;
}

static int mbox_realtek_ipc_set_enabled(const struct device *dev, uint32_t channel, bool enable)
{
	const struct mbox_realtek_ipc_config *cfg = dev->config;
	uint32_t bit = BIT(channel);
	unsigned int key;
	uint32_t imr;

	if (channel >= IPC_NUM_BITS) {
		return -EINVAL;
	}

	key = irq_lock();
	imr = ipc_read(cfg, IPC_REG_IMR);

	if (enable) {
		/* Drop any stale event, then unmask the RX-full interrupt. */
		ipc_write(cfg, IPC_REG_ISR, bit);
		ipc_write(cfg, IPC_REG_IMR, imr | bit);
	} else {
		ipc_write(cfg, IPC_REG_IMR, imr & ~bit);
	}

	irq_unlock(key);

	return 0;
}

static DEVICE_API(mbox, mbox_realtek_ipc_api) = {
	.send = mbox_realtek_ipc_send,
	.register_callback = mbox_realtek_ipc_register_callback,
	.mtu_get = mbox_realtek_ipc_mtu_get,
	.max_channels_get = mbox_realtek_ipc_max_channels_get,
	.set_enabled = mbox_realtek_ipc_set_enabled,
};

static int mbox_realtek_ipc_init(const struct device *dev)
{
	const struct mbox_realtek_ipc_config *cfg = dev->config;

	/* Start with all channels masked; endpoints unmask via set_enabled(). */
	ipc_write(cfg, IPC_REG_IMR, 0U);

	cfg->irq_config_func();

	return 0;
}

#define MBOX_REALTEK_IPC_INIT(inst)						\
	static void mbox_realtek_ipc_irq_config_##inst(void)			\
	{									\
		IRQ_CONNECT(DT_INST_IRQN(inst),					\
			    DT_INST_IRQ(inst, priority),			\
			    mbox_realtek_ipc_isr,				\
			    DEVICE_DT_INST_GET(inst), 0);			\
		irq_enable(DT_INST_IRQN(inst));					\
	}									\
										\
	static const struct mbox_realtek_ipc_config mbox_realtek_ipc_cfg_##inst = { \
		.base = DT_INST_REG_ADDR(inst),					\
		.irq_config_func = mbox_realtek_ipc_irq_config_##inst,		\
	};									\
										\
	static struct mbox_realtek_ipc_data mbox_realtek_ipc_data_##inst;	\
										\
	DEVICE_DT_INST_DEFINE(inst,						\
			      mbox_realtek_ipc_init,				\
			      NULL,						\
			      &mbox_realtek_ipc_data_##inst,			\
			      &mbox_realtek_ipc_cfg_##inst,			\
			      POST_KERNEL,					\
			      CONFIG_MBOX_INIT_PRIORITY,			\
			      &mbox_realtek_ipc_api);

DT_INST_FOREACH_STATUS_OKAY(MBOX_REALTEK_IPC_INIT)
