/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp);

static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    struct bt_mesh_onoff_status *rsp);

static const struct bt_mesh_onoff_srv_handlers onoff_handlers = {
	.set = led_set,
	.get = led_get,
};

struct led_ctx {
	struct bt_mesh_onoff_srv srv;
	struct k_work_delayable work;
	uint32_t remaining;
	bool value;
};

static struct led_ctx led_ctx[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(led_ctx[0].srv, &onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(led_ctx[1].srv, &onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(led_ctx[2].srv, &onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(led_ctx[3].srv, &onoff_handlers) },
#endif
};

static void led_transition_start(struct led_ctx *led)
{
	int led_idx = led - &led_ctx[0];

	/* As long as the transition is in progress, the onoff
	 * state is "on":
	 */
	dk_set_led(led_idx, true);
	k_work_reschedule(&led->work, K_MSEC(led->remaining));
	led->remaining = 0;
}

static void led_status(struct led_ctx *led, struct bt_mesh_onoff_status *status)
{
	/* Do not include delay in the remaining time. */
	status->remaining_time = led->remaining ? led->remaining :
		k_ticks_to_ms_ceil32(k_work_delayable_remaining_get(&led->work));
	status->target_on_off = led->value;
	/* As long as the transition is in progress, the onoff state is "on": */
	status->present_on_off = led->value || status->remaining_time;
}

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	int led_idx = led - &led_ctx[0];

	if (set->on_off == led->value) {
		goto respond;
	}

	led->value = set->on_off;
	if (!bt_mesh_model_transition_time(set->transition)) {
		led->remaining = 0;
		dk_set_led(led_idx, set->on_off);
		goto respond;
	}

	led->remaining = set->transition->time;

	if (set->transition->delay) {
		k_work_reschedule(&led->work, K_MSEC(set->transition->delay));
	} else {
		led_transition_start(led);
	}

respond:
	if (rsp) {
		led_status(led, rsp);
	}
}

static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);

	led_status(led, rsp);
}

static void led_work(struct k_work *work)
{
	struct led_ctx *led = CONTAINER_OF(work, struct led_ctx, work.work);
	int led_idx = led - &led_ctx[0];

	if (led->remaining) {
		led_transition_start(led);
	} else {
		dk_set_led(led_idx, led->value);

		/* Publish the new value at the end of the transition */
		struct bt_mesh_onoff_status status;

		led_status(led, &status);
		bt_mesh_onoff_srv_pub(&led->srv, NULL, &status);
	}
}

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

static struct bt_mesh_elem elements[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
	BT_MESH_ELEM(
		1, BT_MESH_MODEL_PTR_LIST(
			BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_CFG_SRV),
			BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
			BT_MESH_MODEL_DECLARE(BT_MESH_MODEL_RPR_SRV),
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[0].srv)),
		BT_MESH_MODEL_PTR_LIST()),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
	BT_MESH_ELEM(
		2, ((const struct bt_mesh_model *[]) {
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[1].srv)}),
		(const struct bt_mesh_model *[]) {  }),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
	BT_MESH_ELEM(
		3, ((const struct bt_mesh_model *[]) {
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[2].srv)}),
		(const struct bt_mesh_model *[]) { }),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
	BT_MESH_ELEM(
		4, ((const struct bt_mesh_model *[]) {
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[3].srv)}),
		(const struct bt_mesh_model *[]) {  }),
#endif
};

#define COMP_DECLARE(_elem_count) \
	{ \
		.cid = CONFIG_BT_COMPANY_ID, \
		.elem = elements, \
		.elem_count = _elem_count, \
	}

static const struct bt_mesh_comp comps[] = {
	COMP_DECLARE(1), COMP_DECLARE(2), COMP_DECLARE(3), COMP_DECLARE(4)
};

static uint8_t leds_comp;

static int leds_load_cb(const char *name, size_t len_rd, settings_read_cb read_cb, void *cb_arg,
			void *param)
{
	static bool leds_loaded;

	if (leds_loaded) {
		return 0; // Already loaded
	}

	ssize_t len;

	len = read_cb(cb_arg, &leds_comp, sizeof(leds_comp));
	if (len < 0) {
		printk("Failed to read value (err %zd)", len);
		return len;
	}

	printk("Loaded LEDs: %d\n", leds_comp + 1);

	if (len != len_rd) {
		printk("Unexpected value length (%zd != %zu)", len, len_rd);
		return -EINVAL;
	}

	leds_loaded = true;

	return 0;
}

const struct bt_mesh_comp *model_handler_init(void)
{
	int err;

	k_work_init_delayable(&attention_blink_work, attention_blink);

	for (int i = 0; i < ARRAY_SIZE(led_ctx); ++i) {
		k_work_init_delayable(&led_ctx[i].work, led_work);
	}

	err = settings_load_subtree_direct("app/leds", leds_load_cb, NULL);
	if (err) {
		printk("Failed to load LED settings (err %d)\n", err);
		return &comps[0];
	}

	return &comps[leds_comp];
}

static void comp_reg_and_store_handler(struct k_work *work)
{
	int err;

	printk("LEDs supported: %d\n", leds_comp + 1);

	/* FIXME: This doesn't handle the case when reboot happens before reprovisioning.
	 * In this case, the old composition data should be passed.
	 */
	err = bt_mesh_comp128_register(&comps[leds_comp]);
	if (err) {
		printk("Failed to register LED component (err %d)\n", err);
		return;
	}

	err = settings_save_one("app/leds", &leds_comp, sizeof(leds_comp));
	if (err) {
		printk("Failed to save LED settings (err %d)\n", err);
		return;
	}
}

static K_WORK_DEFINE(comp_reg_and_store, comp_reg_and_store_handler);

static int cmd_led_add(const struct shell *shell, size_t argc, char *argv[])
{
	if (leds_comp >= ARRAY_SIZE(comps)) {
		shell_error(shell, "Maximum number of LEDs reached");
		return -ENOMEM;
	}

	leds_comp++;
	k_work_submit(&comp_reg_and_store);

	return 0;
}

static int cmd_led_remove(const struct shell *shell, size_t argc, char *argv[])
{
	if (leds_comp == 0) {
		shell_error(shell, "Maximum number of LEDs reached");
		return -ENOMEM;
	}

	leds_comp--;
	k_work_submit(&comp_reg_and_store);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_cmds,
	SHELL_CMD_ARG(led_add, NULL, "Add led", cmd_led_add, 1, 0),
	SHELL_CMD_ARG(led_remove, NULL, "Remove led", cmd_led_remove, 1, 0),

	SHELL_SUBCMD_SET_END
);

static int cmd_app(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(shell);
		/* shell returns 1 when help is printed */
		return 1;
	}

	shell_error(shell, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(app, &app_cmds, "App commands", cmd_app, 1, 1);
