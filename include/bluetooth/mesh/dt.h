/** @file
 *  @brief DT API for Access layer.
 */

/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_MESH_DT_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_MESH_DT_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h>
#include <zephyr/bluetooth/mesh/access.h>

#include <bluetooth/mesh/models.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define your mesh element nodes */
#define MESH_NODE DT_PATH(mesh) //DT_NODELABEL(mesh)

#define EXPAND(x) x

#define bt_mesh_model_cfg_srv_MACRO(model) EXPAND(BT_MESH_MODEL_CFG_SRV)
#define bt_mesh_model_health_srv_MACRO(model) EXPAND(BT_MESH_MODEL_HEALTH_SRV(&DT_STRING_TOKEN(model, vname), DT_STRING_UNQUOTED_BY_IDX(model, vargs, 0)))
#define bt_mesh_model_onoff_cli_MACRO(model) EXPAND(BT_MESH_MODEL_ONOFF_CLI(&DT_STRING_TOKEN(model, vname)))
#define bt_mesh_model_ponoff_srv_MACRO(model) EXPAND(BT_MESH_MODEL_PONOFF_SRV(&DT_STRING_TOKEN(model, vname)))
#define bt_mesh_model_lightness_srv_MACRO(model) EXPAND(BT_MESH_MODEL_LIGHTNESS_SRV(&DT_STRING_UNQUOTED(model, vname)))
#define bt_mesh_model_light_ctrl_srv_MACRO(model) EXPAND(BT_MESH_MODEL_LIGHT_CTRL_SRV(&DT_STRING_TOKEN(model, vname)))
#define bt_mesh_model_scene_srv_MACRO(model) EXPAND(BT_MESH_MODEL_SCENE_SRV(&DT_STRING_TOKEN(model, vname)))
#define bt_mesh_model_sensor_srv_MACRO(model) EXPAND(BT_MESH_MODEL_SENSOR_SRV(&DT_STRING_TOKEN(model, vname)))

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

#define DT_BT_MESH_ELEMS DT_FOREACH_CHILD_SEP(DT_CHILD(MESH_NODE, elements), DT_BT_MESH_ELEM_INIT, (,))

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_MESH_DT_H_ */
