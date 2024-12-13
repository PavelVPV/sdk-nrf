/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>

// FIXME:...
#define CONFIG_BT_MESH_USES_TINYCRYPT 1

#include <zephyr/bluetooth/mesh.h>
#include <bluetooth/mesh/models.h>

#include <zephyr/devicetree.h>

static struct bt_mesh_onoff_cli onoff_cli;
static struct bt_mesh_ponoff_srv ponoff_srv;

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw3))
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

/* Define your mesh element nodes */
#define MESH_NODE DT_PATH(mesh) //DT_NODELABEL(mesh)

/* Find a node that is compatible with "bt,mesh-model-health-srv" */
#define CFG_SRV_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(bt_mesh_model_cfg_srv)

#define EXPAND(x) x

#define bt_mesh_model_cfg_srv_MACRO(model) EXPAND(BT_MESH_MODEL_CFG_SRV)
#define bt_mesh_model_health_srv_MACRO(model) EXPAND(BT_MESH_MODEL_HEALTH_SRV(&DT_STRING_TOKEN(model, vname), DT_STRING_UNQUOTED_BY_IDX(model, vargs, 0)))
#define bt_mesh_model_onoff_cli_MACRO(model) EXPAND(BT_MESH_MODEL_ONOFF_CLI(&DT_STRING_TOKEN(model, vname)))
#define bt_mesh_model_ponoff_srv_MACRO(model) EXPAND(BT_MESH_MODEL_PONOFF_SRV(&DT_STRING_TOKEN(model, vname)))


//#define MACRO_FROM_COMPAT(compat) compat##_MACRO

//#define DT_BT_MESH_MODEL_INIT(model) DT_PROP(model, compatible)##_MACRO,

// Helper macro to concatenate tokens
#define CONCAT2(a, b) a##b
#define EXPAND_AND_CONCAT(a, b) CONCAT2(a, b)

/**
 * @param model - dts model
 */
#define DT_BT_MESH_MODEL_INIT(model) EXPAND_AND_CONCAT(DT_STRING_TOKEN(model, cmodel), _MACRO)(model),

/**
 * @param element - dts element
 */
#define MODELS(element) \
	((struct bt_mesh_model[]) { DT_FOREACH_CHILD(DT_CHILD(element, models), DT_BT_MESH_MODEL_INIT) })

#define DT_BT_MESH_ELEM_INIT(element) { \
	.rt		  = &(struct bt_mesh_elem_rt_ctx) { 0 },	\
	.loc              = (DT_PROP(element, location)),			\
	.model_count      = ARRAY_SIZE(MODELS(element)),				\
	.vnd_model_count  = 0,				\
	.models           = MODELS(element),					\
	.vnd_models       = NULL, \
}

struct bt_mesh_elem elements[] = {
	DT_FOREACH_CHILD_SEP(DT_CHILD(MESH_NODE, elements), DT_BT_MESH_ELEM_INIT, (,))
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

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

	err = dk_buttons_init(NULL);
	if (err) {
		printk("Initializing buttons failed (err %d)\n", err);
		return;
	}

	err = bt_mesh_init(bt_mesh_dk_prov_init(), &comp);
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_MESH_LOW_POWER)) {
		bt_mesh_lpn_set(true);
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* This will be a no-op if settings_load() loaded provisioning info */
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");
}

int main(void)
{
	int err;

	printk("Initializing...\n");

	k_work_init_delayable(&attention_blink_work, attention_blink);

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	return 0;
}

#if 0
int main(void)
{
#if 0
	/* Extract Element and Model Data */
	const struct device *mesh_dev = device_get_binding(DT_LABEL(MESH_NODE));

	if (mesh_dev == NULL) {
		printk("Failed to get mesh device\n");
		return;
	}

	/* Element 1 */
	const int element_1_reg = DT_PROP(DT_CHILD(MESH_NODE, element_1), reg);
	const char *model_cfg_srv = DT_PROP(DT_CHILD(DT_CHILD(MESH_NODE, element_1), model_cfg_srv), compatible);

	/* Element 2 */
	const int element_2_reg = DT_PROP(DT_CHILD(MESH_NODE, element_2), reg);
	const char *model_onoff_cli_1 = DT_PROP(DT_CHILD(DT_CHILD(MESH_NODE, element_2), model_onoff_cli_1), compatible);
#endif

	/* Further elements and models can be accessed in a similar way */

	if (DT_NODE_EXISTS(CFG_SRV_NODE)) {
		/* Node exists, you can now use this node */
		printk("Config Server node found!\n");
	} else {
		printk("Config Server node not found!\n");
	}

	return 0;
}
#endif
