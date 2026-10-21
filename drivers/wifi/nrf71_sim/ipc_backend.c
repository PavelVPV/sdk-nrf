/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#define DT_DRV_COMPAT nordic_nrf71_sim_ipc

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service_backend.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "umac_sim.h"

LOG_MODULE_REGISTER(nrf71_sim, CONFIG_WIFI_NRF71_SIM_LOG_LEVEL);

/* Must match the private types in nrf71/src/common/wifi_ipc.c. */
struct sim_ipc_ring_info {
	uint32_t tail_addr;
	uint32_t base;
	uint32_t size;
	bool padded;
};

struct sim_ipc_buf_desc {
	uint32_t addr;
	uint32_t size;
	uint32_t *ack_addr;
	struct sim_ipc_ring_info ring;
};

BUILD_ASSERT(sizeof(void *) == 4, "nRF71 IPC uses 32-bit addresses, use native_sim/native");
BUILD_ASSERT(sizeof(struct sim_ipc_buf_desc) == 28);

#define SIM_RING_SIZE 16384

static uint8_t evt_ring[SIM_RING_SIZE] __aligned(4);
static volatile uint32_t evt_ring_tail;

static const struct ipc_ept_cfg *ept_cfg;

K_THREAD_STACK_DEFINE(umac_sim_wq_stack, 8192);
struct k_work_q umac_sim_wq;

struct sim_msg_work {
	struct k_work_delayable dwork;
	bool is_event;
	size_t len;
	uint8_t data[];
};

static void sim_msg_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct sim_msg_work *item = CONTAINER_OF(dwork, struct sim_msg_work, dwork);

	if (item->is_event) {
		umac_sim_evt_send(item->data, item->len);
	} else {
		umac_sim_handle_msg(item->data, item->len);
	}

	k_free(item);
}

static int sim_queue(const void *data, size_t len, bool is_event, uint32_t delay_ms)
{
	struct sim_msg_work *item = k_malloc(sizeof(*item) + len);

	if (!item) {
		LOG_ERR("No memory for %s of %zu B", is_event ? "event" : "command", len);
		return -ENOMEM;
	}

	item->is_event = is_event;
	item->len = len;
	memcpy(item->data, data, len);
	k_work_init_delayable(&item->dwork, sim_msg_work_handler);
	k_work_schedule_for_queue(&umac_sim_wq, &item->dwork, K_MSEC(delay_ms));

	return 0;
}

void umac_sim_evt_send_delayed(const void *msg, size_t len, uint32_t delay_ms)
{
	(void)sim_queue(msg, len, true, delay_ms);
}

void umac_sim_evt_send(const void *msg, size_t len)
{
	struct sim_ipc_buf_desc desc = { 0 };
	uint32_t size = ROUND_UP(len, 4);
	uint32_t offset = evt_ring_tail;

	if (!ept_cfg || !ept_cfg->cb.received) {
		return;
	}

	if (size > SIM_RING_SIZE) {
		LOG_ERR("Event of %zu B does not fit the ring", len);
		return;
	}

	/* The host frees each event inside the callback, so the ring is empty here. */
	if (offset + size > SIM_RING_SIZE) {
		desc.ring.padded = true;
		offset = 0;
	}

	memcpy(&evt_ring[offset], msg, len);

	desc.addr = (uint32_t)&evt_ring[offset];
	desc.size = size;
	desc.ring.tail_addr = (uint32_t)&evt_ring_tail;
	desc.ring.base = (uint32_t)evt_ring;
	desc.ring.size = SIM_RING_SIZE;

	ept_cfg->cb.received(&desc, sizeof(desc), ept_cfg->priv);
}

static void sim_bind_handler(struct k_work *work)
{
	if (ept_cfg && ept_cfg->cb.bound) {
		ept_cfg->cb.bound(ept_cfg->priv);
	}
}

static K_WORK_DELAYABLE_DEFINE(sim_bind_work, sim_bind_handler);

static int sim_open_instance(const struct device *instance)
{
	return 0;
}

static int sim_register_endpoint(const struct device *instance, void **token,
				 const struct ipc_ept_cfg *cfg)
{
	if (ept_cfg) {
		return -EALREADY;
	}

	ept_cfg = cfg;
	*token = (void *)cfg;
	k_work_schedule_for_queue(&umac_sim_wq, &sim_bind_work, K_MSEC(1));

	return 0;
}

static int sim_send(const struct device *instance, void *token, const void *data, size_t len)
{
	const struct sim_ipc_buf_desc *desc = data;
	int ret;

	if (len != sizeof(*desc)) {
		return -EINVAL;
	}

	ret = sim_queue((const void *)desc->addr, desc->size, false, 0);
	if (ret) {
		return ret;
	}

	/* The message is copied, so the host can free it. */
	*desc->ack_addr = desc->addr;

	return 0;
}

static int sim_init(const struct device *dev)
{
	k_work_queue_start(&umac_sim_wq, umac_sim_wq_stack,
			   K_THREAD_STACK_SIZEOF(umac_sim_wq_stack),
			   K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1), NULL);
	k_thread_name_set(k_work_queue_thread_get(&umac_sim_wq), "umac_sim");

	return 0;
}

static const struct ipc_service_backend sim_backend_ops = {
	.open_instance = sim_open_instance,
	.register_endpoint = sim_register_endpoint,
	.send = sim_send,
};

DEVICE_DT_INST_DEFINE(0, sim_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_backend_ops);
