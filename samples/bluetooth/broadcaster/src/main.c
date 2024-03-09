/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/init.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/settings/settings.h>

#define BT_DATA_MESH_MESSAGE            0x2a /**< Mesh Networking PDU */

static void pending_adv_start(struct k_work *work);
static K_WORK_DEFINE(pending_adv, pending_adv_start);

static struct bt_le_ext_adv *adv;
static struct bt_le_adv_param adv_params = {
	.id = BT_ID_DEFAULT,
	.sid = 0,
	.secondary_max_skip = 0,
	.options = BT_LE_ADV_OPT_USE_NAME,
	.interval_min = (20 * 8) / 5,
	.interval_max = (20 * 8) / 5,
	.peer = NULL
};

int adv_send(void)
{
	static uint8_t data[] = "12345";

	struct bt_data ad;
	int err;

	struct bt_le_ext_adv_start_param start = {
		.num_events = 1,
	};

	ad.type = BT_DATA_MESH_MESSAGE;
	ad.data_len = sizeof(data);
	ad.data = data;

	err = bt_le_ext_adv_set_data(adv, &ad, 1, NULL, 0);
	if (err) {
		printk("Setting adv data failed (err %d)\n", err);
		return err;
	}

	err = bt_le_ext_adv_start(adv, &start);
	if (err) {
		printk("Starting advertising failed (err %d)\n", err);
	}

	return err;
}

static void pending_adv_start(struct k_work *work)
{
	int err;

	err = adv_send();
	if (err) {
		printk("Failed to send adv: %d\n", err);
	}
}

static void adv_sent(struct bt_le_ext_adv *instance,
		     struct bt_le_ext_adv_sent_info *info)
{
	k_work_submit(&pending_adv);
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
		    uint8_t adv_type, struct net_buf_simple *buf)
{
}

int scan_enable(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_HCI_LE_SCAN_PASSIVE,
		.interval = (30 * 8) / 5,
		.window = (30 * 8) / 5,
	};
	int err;

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		printk("starting scan failed (err %d)", err);
	}

	return err;
}

static void settings_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(settings_work, settings_work_handler);
static void settings_work_handler(struct k_work *work)
{
	uint8_t idx[100];
	static int i;
	int err;

	for (int j = 0; j < 100; j++) {
		idx[j] = i;
	}

	i++;

	err = settings_save_one("app/tmp", &idx, sizeof(idx));
	if (err) {
		printk("settings_save_one failed: %d\n", err);
	}

	k_work_reschedule(&settings_work, K_MSEC(100));
}

static int start(void)
{
	static const struct bt_le_ext_adv_cb adv_cb = {
		.sent = adv_sent,
	};
	int err;

	err = bt_le_ext_adv_create(&adv_params, &adv_cb, &adv);
	if (err) {
		printk("Creating adv instance failed (err %d)\n", err);
		return err;
	}

	printk("Adv ext created\n");

	scan_enable();

	k_work_submit(&pending_adv);

	printk("First adv sent\n");

	k_work_schedule(&settings_work, K_MSEC(100));

	return err;
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	printk("Bluetooth initialized\n");

	err = start();
	if (err) {
		printk("Failed to start sample: %d\n", err);
	}
}

int main(void)
{
	int err;

	printk("Initializing...\n");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	return 0;
}
