/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>

#include "dfu_target.h"
#include "model_handler.h"

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

	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");

	/* Confirm the image and mark it as applied after the mesh started. */
	dfu_target_image_confirm();
}

int main(void)
{
	int err;

	printk("Initializing...\n");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 2;
	}

	return 0;
}
