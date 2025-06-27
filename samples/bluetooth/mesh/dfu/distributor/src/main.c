/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Bluetooth Mesh DFU Distributor role sample
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/shell.h>
#include <bluetooth/mesh/vnd/le_pair_resp.h>
#include <bluetooth/mesh/dk_prov.h>
#include <bluetooth/mesh/models.h>
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

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
}

static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    struct bt_mesh_onoff_status *rsp)
{
}

static const struct bt_mesh_onoff_srv_handlers onoff_handlers = {
	.set = led_set,
	.get = led_get,
};

static struct bt_mesh_onoff_srv onoff_srv = BT_MESH_ONOFF_SRV_INIT(onoff_srv, &onoff_handlers);

static struct bt_mesh_cfg_cli cfg_cli;

static struct bt_mesh_rpr_cli rpr_cli = BT_MESH_RPR_CLI_INIT(rpr_cli);

#if 1
static const struct bt_mesh_model * primary_models[] = {
	BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_CFG_SRV),
	BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
	BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_CFG_CLI(&cfg_cli)),
	BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_RPR_SRV),
	BT_MESH_MODEL_DFD_SRV(&dfd_srv),
	BT_MESH_MODEL_RPR_CLI(&rpr_cli),
	BT_MESH_MODEL_ONOFF_SRV(&onoff_srv),
};

static const struct bt_mesh_model * primary_vnd_models[] = {
#if CONFIG_BT_MESH_LE_PAIR_RESP
	BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_LE_PAIR_RESP),
#endif
};

static const struct bt_mesh_model * secondary_models[] = {
	BT_MESH_MODEL_DFU_SRV(&dfu_srv),
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1, primary_models,
		     primary_vnd_models),
	BT_MESH_ELEM(2, secondary_models,
		     BT_MESH_MODEL_PTR_LIST()),
};
#else
static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1, BT_MESH_MODEL_PTR_LIST(BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_CFG_SRV),
					       BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
					       BT_MESH_MODEL_DFD_SRV(&dfd_srv),
					       BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_RPR_CLI(&bt_mesh_shell_rpr_cli))),
		     BT_MESH_MODEL_PTR_LIST(
#if CONFIG_BT_MESH_LE_PAIR_RESP
					    BT_MESH_MODEL_LE_PAIR_RESP,
#endif
					    )),
	BT_MESH_ELEM(2, BT_MESH_MODEL_PTR_LIST(BT_MESH_MODEL_DFU_SRV(&dfu_srv)),
		     BT_MESH_MODEL_PTR_LIST()),
};
#endif


static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void reboot_work_handler(struct k_work *work)
{
	printk("Rebooting...\n");
	sys_reboot(SYS_REBOOT_WARM);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static void reprovisioned(uint16_t addr)
{
	printk("Reprovisioned by RPR client, address: 0x%04x\n", addr);
	k_work_schedule(&reboot_work, K_SECONDS(3));
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = dk_leds_init();
	if (err) {
		printk("Initializing LEDs failed (err %d)\n", err);
		return;
	}

	bt_mesh_shell_prov.reprovisioned = reprovisioned;

	err = bt_mesh_init(&bt_mesh_shell_prov, &comp);
//	err = bt_mesh_init(bt_mesh_dk_prov_init(), &comp);
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");

	/* Start advertising SMP BT service. */
	err = smp_dfu_init();
	if (err) {
		printk("SMP initialization failed (err: %d)\n", err);
	}

	/* Confirm the image and mark it as applied after the mesh started. */
	dfu_target_image_confirm();
}

int main(void)
{
	int err;

	printk("Initializing...\n");

	err = bt_mesh_blob_io_flash_init(&blob_flash_stream,
					 FIXED_PARTITION_ID(slot1_partition), 0);
	if (err) {
		printk("Failed to init BLOB IO Flash module: %d\n", err);
		return 1;
	}

	k_work_init_delayable(&attention_blink_work, attention_blink);
	dfu_distributor_init(&blob_flash_stream);
	dfu_target_init(&blob_flash_stream);

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 2;
	}
	return 0;
}
