/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/storage/flash_map.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>

#include "smp_bt.h"
#include "dfu_dist.h"
#include "dfu_target.h"

static struct bt_mesh_blob_io_flash blob_flash_stream;

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
		BIT(3),
#endif
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
	}
}

static void attention_on(const struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(const struct bt_mesh_model *mod)
{
	/* Will stop rescheduling blink timer */
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_model primary_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
	BT_MESH_MODEL_DFD_SRV(&dfd_srv),
};

static struct bt_mesh_model primary_vnd_models[] = {
#if CONFIG_BT_MESH_LE_PAIR_RESP
	BT_MESH_MODEL_LE_PAIR_RESP,
#endif
};

static struct bt_mesh_model secondary_models[] = {
	BT_MESH_MODEL_DFU_SRV(&dfu_srv),
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1, primary_models, primary_vnd_models),
	BT_MESH_ELEM(2, secondary_models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	int err;

	err = bt_mesh_blob_io_flash_init(&blob_flash_stream, FIXED_PARTITION_ID(slot1_partition),
					 0);
	if (err) {
		printk("Failed to init BLOB IO Flash module: %d\n", err);
		return NULL;
	}

	k_work_init_delayable(&attention_blink_work, attention_blink);
	dfu_distributor_init(&blob_flash_stream);
	dfu_target_init(&blob_flash_stream);

	return &comp;
}
