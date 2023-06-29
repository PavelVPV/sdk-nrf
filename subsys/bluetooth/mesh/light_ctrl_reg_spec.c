/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <bluetooth/mesh/light_ctrl_reg_spec.h>

#define LOG_LEVEL 4//CONFIG_BT_MESH_MODEL_LOG_LEVEL
#include "zephyr/logging/log.h"
LOG_MODULE_REGISTER(bt_mesh_light_ctrl_reg_spec);

#define REG_INT CONFIG_BT_MESH_LIGHT_CTRL_REG_SPEC_INTERVAL

#define I_VAL(input,ki) ((input) * (ki) * ((float)REG_INT / (float)MSEC_PER_SEC))

struct reg_params {
	float input;
	float kp;
	float ki;
};

static struct reg_params reg_params_get(struct bt_mesh_light_ctrl_reg_spec *spec_reg)
{
	float target = bt_mesh_light_ctrl_reg_target_get(&spec_reg->reg);
	float error = target - spec_reg->reg.measured;
	/* Accuracy should be in percent and both up and down: */
	float accuracy = (spec_reg->reg.cfg.accuracy * target) / (2 * 100.0f);
	float input;
	float kp, ki;

	if (error > accuracy) {
		input = error - accuracy;
	} else if (error < -accuracy) {
		input = error + accuracy;
	} else {
		input = 0.0f;
	}

	if (input >= 0) {
		kp = spec_reg->reg.cfg.kp.up;
		ki = spec_reg->reg.cfg.ki.up;
	} else {
		kp = spec_reg->reg.cfg.kp.down;
		ki = spec_reg->reg.cfg.ki.down;
	}

	return (struct reg_params){
		.input = input,
		.kp = kp,
		.ki = ki,
	};
}

static void reg_step(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bt_mesh_light_ctrl_reg_spec *spec_reg = CONTAINER_OF(
		dwork, struct bt_mesh_light_ctrl_reg_spec, timer);
	struct reg_params reg_params;

	if (!spec_reg->enabled) {
		/* The regulator might be disabled asynchronously. */
		return;
	}

	k_work_reschedule(&spec_reg->timer, K_MSEC(REG_INT));

	reg_params = reg_params_get(spec_reg);

	spec_reg->i += I_VAL(reg_params.input, reg_params.ki);
	spec_reg->i = CLAMP(spec_reg->i, 0, UINT16_MAX);

	float p = reg_params.input * reg_params.kp;
	float output = spec_reg->i + p;

	spec_reg->reg.updated(&spec_reg->reg, output);
}

static void internal_sum_recover(struct bt_mesh_light_ctrl_reg_spec *spec_reg, uint16_t lightness)
{
	struct reg_params reg_params;
	float i_val, p_val;

	reg_params = reg_params_get(spec_reg);

	i_val = I_VAL(reg_params.input, reg_params.ki);
	p_val = reg_params.input * reg_params.kp;

	/* L = In + U*Kp => In = L - U*Kp;
	 * In = In-1 + U*T_Ki => In - U*T*Ki;
	 * In-1 = L - U*Kp - U*T*Ki.
	 */
	spec_reg->i = lightness - p_val - i_val;
	LOG_ERR("lightness: %d, recaled val: %d", lightness, spec_reg->i);
}

void bt_mesh_light_ctrl_reg_spec_start(struct bt_mesh_light_ctrl_reg *reg, uint16_t lightness)
{
	struct bt_mesh_light_ctrl_reg_spec *spec_reg = CONTAINER_OF(
		reg, struct bt_mesh_light_ctrl_reg_spec, reg);
	spec_reg->enabled = true;
	k_work_schedule(&spec_reg->timer, K_MSEC(REG_INT));
	internal_sum_recover(spec_reg, lightness);
}

void bt_mesh_light_ctrl_reg_spec_stop(struct bt_mesh_light_ctrl_reg *reg)
{
	struct bt_mesh_light_ctrl_reg_spec *spec_reg = CONTAINER_OF(
		reg, struct bt_mesh_light_ctrl_reg_spec, reg);
	spec_reg->i = 0;
	spec_reg->enabled = false;
	k_work_cancel_delayable(&spec_reg->timer);
}

void bt_mesh_light_ctrl_reg_spec_init(struct bt_mesh_light_ctrl_reg *reg)
{
	struct bt_mesh_light_ctrl_reg_spec *spec_reg = CONTAINER_OF(
		reg, struct bt_mesh_light_ctrl_reg_spec, reg);
	k_work_init_delayable(&spec_reg->timer, reg_step);
}
