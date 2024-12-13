/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>

// FIXME:...
#define CONFIG_BT_MESH_USES_TINYCRYPT 1

#include <zephyr/bluetooth/mesh.h>
#include <bluetooth/mesh/models.h>

#include <zephyr/devicetree.h>

static struct bt_mesh_onoff_cli onoff_cli;

/* Define your mesh element nodes */
#define MESH_NODE DT_PATH(mesh) //DT_NODELABEL(mesh)

/* Find a node that is compatible with "bt,mesh-model-health-srv" */
#define CFG_SRV_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(bt_mesh_model_cfg_srv)

#define EXPAND(x) x

#define bt_mesh_model_cfg_srv_MACRO(name) EXPAND(BT_MESH_MODEL_CFG_SRV)
#define bt_mesh_model_health_srv_MACRO(name) EXPAND(BT_MESH_MODEL_HEALTH_SRV(NULL, NULL))
#define bt_mesh_model_onoff_cli_MACRO(name) EXPAND(BT_MESH_MODEL_ONOFF_CLI(&name))


//#define MACRO_FROM_COMPAT(compat) compat##_MACRO

//#define DT_BT_MESH_MODEL_INIT(model) DT_PROP(model, compatible)##_MACRO,

// Helper macro to concatenate tokens
#define CONCAT2(a, b) a##b
#define EXPAND_AND_CONCAT(a, b) CONCAT2(a, b)

/**
 * @param model - dts model
 */
#define DT_BT_MESH_MODEL_INIT(model) EXPAND_AND_CONCAT(DT_STRING_TOKEN(model, cmodel), _MACRO)(DT_STRING_TOKEN(model, vname)),

/**
 * @param element - dts element
 */
#define MODELS(element) \
	((struct bt_mesh_model[]) { DT_FOREACH_CHILD(DT_CHILD(element, models), DT_BT_MESH_MODEL_INIT) })

#define DT_BT_MESH_ELEM_INIT(element) { \
	.rt		  = &(struct bt_mesh_elem_rt_ctx) { 0 },	\
	.loc              = (DT_PROP(element, location)),			\
	.model_count      = DT_CHILD_NUM(DT_CHILD(element, models)),				\
	.vnd_model_count  = 0,				\
	.models           = MODELS(element),					\
	.vnd_models       = NULL, \
},

struct bt_mesh_elem elements[] = {
	DT_FOREACH_CHILD_SEP(DT_CHILD(MESH_NODE, elements), DT_BT_MESH_ELEM_INIT, (,))
};

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
