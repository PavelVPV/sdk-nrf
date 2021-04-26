/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <bluetooth/mesh/gen_ponoff_srv.h>

#include <string.h>
#include <settings/settings.h>
#include <stdlib.h>
#include "model_utils.h"

/** Persistent storage handling */
struct ponoff_settings_data {
	uint8_t on_power_up;
	bool on_off;
} __packed;

static int store(struct bt_mesh_ponoff_srv *srv,
		 const struct bt_mesh_onoff_status *onoff_status)
{
	if (!IS_ENABLED(CONFIG_BT_SETTINGS)) {
		return 0;
	}

	struct ponoff_settings_data data;
	ssize_t size;

	data.on_power_up = (uint8_t)srv->on_power_up;

	switch (srv->on_power_up) {
	case BT_MESH_ON_POWER_UP_OFF:
		data.on_off = false;
		break;
	case BT_MESH_ON_POWER_UP_ON:
		data.on_off = true;
		break;
	case BT_MESH_ON_POWER_UP_RESTORE:
		data.on_off = onoff_status->remaining_time > 0 ?
				      onoff_status->target_on_off :
				      onoff_status->present_on_off;
		break;
	default:
		return -EINVAL;
	}

	/* Models that extend Generic Power OnOff Server and, which states are
	 * bound with Generic OnOff state, store the value of the bound state
	 * separately, therefore they don't need to store Generic OnOff state.
	 */
	if (bt_mesh_model_is_extended(srv->ponoff_model)) {
		size = sizeof(data.on_power_up);
	} else {
		size = sizeof(data);
	}

	return bt_mesh_model_data_store(srv->ponoff_model, false, NULL, &data,
					size);
}

static void send_rsp(struct bt_mesh_ponoff_srv *srv,
		     struct bt_mesh_msg_ctx *ctx)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_PONOFF_OP_STATUS,
				 BT_MESH_PONOFF_MSG_LEN_STATUS);
	bt_mesh_model_msg_init(&msg, BT_MESH_PONOFF_OP_STATUS);
	net_buf_simple_add_u8(&msg, srv->on_power_up);
	bt_mesh_model_send(srv->ponoff_model, ctx, &msg, NULL, NULL);
}

static void handle_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{
	if (buf->len != BT_MESH_PONOFF_MSG_LEN_GET) {
		return;
	}

	send_rsp(model->user_data, ctx);
}

static void set_on_power_up(struct bt_mesh_ponoff_srv *srv,
			    struct bt_mesh_msg_ctx *ctx,
			    enum bt_mesh_on_power_up new)
{
	if (new == srv->on_power_up) {
		return;
	}

	enum bt_mesh_on_power_up old = srv->on_power_up;

	srv->on_power_up = new;

	bt_mesh_model_msg_init(srv->ponoff_model->pub->msg,
			       BT_MESH_PONOFF_OP_STATUS);
	net_buf_simple_add_u8(srv->ponoff_model->pub->msg, srv->on_power_up);

	if (srv->update) {
		srv->update(srv, ctx, old, new);
	}

	struct bt_mesh_onoff_status onoff_status = { 0 };

	if (!bt_mesh_model_is_extended(srv->ponoff_model)) {
		srv->onoff.handlers->get(&srv->onoff, NULL, &onoff_status);
	}

	store(srv, &onoff_status);
}

static void handle_set_msg(struct bt_mesh_model *model,
			   struct bt_mesh_msg_ctx *ctx,
			   struct net_buf_simple *buf, bool ack)
{
	if (buf->len != BT_MESH_PONOFF_MSG_LEN_SET) {
		return;
	}

	struct bt_mesh_ponoff_srv *srv = model->user_data;
	enum bt_mesh_on_power_up new = net_buf_simple_pull_u8(buf);

	if (new >= BT_MESH_ON_POWER_UP_INVALID) {
		return;
	}

	set_on_power_up(srv, ctx, new);

	if (ack) {
		send_rsp(model->user_data, ctx);
	}

	(void)bt_mesh_ponoff_srv_pub(srv, NULL);
}

static void handle_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{
	handle_set_msg(model, ctx, buf, true);
}

static void handle_set_unack(struct bt_mesh_model *model,
			     struct bt_mesh_msg_ctx *ctx,
			     struct net_buf_simple *buf)
{
	handle_set_msg(model, ctx, buf, false);
}

/* Need to intercept the onoff state to get the right value on power up. */
static void onoff_intercept_set(struct bt_mesh_onoff_srv *onoff_srv,
				struct bt_mesh_msg_ctx *ctx,
				const struct bt_mesh_onoff_set *set,
				struct bt_mesh_onoff_status *status)
{
	struct bt_mesh_ponoff_srv *srv =
		CONTAINER_OF(onoff_srv, struct bt_mesh_ponoff_srv, onoff);

	srv->onoff_handlers->set(onoff_srv, ctx, set, status);

	if ((srv->on_power_up == BT_MESH_ON_POWER_UP_RESTORE) &&
	    !bt_mesh_model_is_extended(srv->ponoff_model)) {
		store(srv, status);
	}
}

static void onoff_intercept_get(struct bt_mesh_onoff_srv *onoff_srv,
				struct bt_mesh_msg_ctx *ctx,
				struct bt_mesh_onoff_status *out)
{
	struct bt_mesh_ponoff_srv *srv =
		CONTAINER_OF(onoff_srv, struct bt_mesh_ponoff_srv, onoff);

	srv->onoff_handlers->get(onoff_srv, ctx, out);
}

const struct bt_mesh_model_op _bt_mesh_ponoff_srv_op[] = {
	{ BT_MESH_PONOFF_OP_GET, BT_MESH_PONOFF_MSG_LEN_GET, handle_get },
	BT_MESH_MODEL_OP_END,
};

const struct bt_mesh_model_op _bt_mesh_ponoff_setup_srv_op[] = {
	{
		BT_MESH_PONOFF_OP_SET,
		BT_MESH_PONOFF_MSG_LEN_SET,
		handle_set,
	},
	{
		BT_MESH_PONOFF_OP_SET_UNACK,
		BT_MESH_PONOFF_MSG_LEN_SET,
		handle_set_unack,
	},
	BT_MESH_MODEL_OP_END,
};

const struct bt_mesh_onoff_srv_handlers _bt_mesh_ponoff_onoff_intercept = {
	.set = onoff_intercept_set,
	.get = onoff_intercept_get,
};

static ssize_t scene_store(struct bt_mesh_model *model, uint8_t data[])
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;
	struct bt_mesh_onoff_status status = { 0 };

	/* Only store the next stable on_off state: */
	srv->onoff_handlers->get(&srv->onoff, NULL, &status);
	data[0] = status.remaining_time ? status.target_on_off :
					  status.present_on_off;

	return 1;
}

static void scene_recall(struct bt_mesh_model *model, const uint8_t data[],
			 size_t len, struct bt_mesh_model_transition *transition)
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;
	struct bt_mesh_onoff_status status = { 0 };
	struct bt_mesh_onoff_set set = {
		.on_off = data[0],
		.transition = transition,
	};

	srv->onoff_handlers->set(&srv->onoff, NULL, &set, &status);

	(void)bt_mesh_onoff_srv_pub(&srv->onoff, NULL, &status);
}

const struct bt_mesh_scene_entry_type scene_type = {
	.maxlen = 1,
	.store = scene_store,
	.recall = scene_recall,
};

static int update_handler(struct bt_mesh_model *model)
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;

	bt_mesh_model_msg_init(srv->ponoff_model->pub->msg,
			       BT_MESH_PONOFF_OP_STATUS);
	net_buf_simple_add_u8(srv->ponoff_model->pub->msg, srv->on_power_up);
	return 0;
}

static int bt_mesh_ponoff_srv_init(struct bt_mesh_model *model)
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;

	srv->ponoff_model = model;
	srv->pub.msg = &srv->pub_buf;
	srv->pub.update = update_handler;
	net_buf_simple_init_with_data(&srv->pub_buf, srv->pub_data,
				      sizeof(srv->pub_data));

	/* Model extensions:
	 * To simplify the model extension tree, we're flipping the
	 * relationship between the ponoff server and the ponoff setup
	 * server. In the specification, the ponoff setup server extends
	 * the ponoff server, which is the opposite of what we're doing
	 * here. This makes no difference for the mesh stack, but it
	 * makes it a lot easier to extend this model, as we won't have
	 * to support multiple extenders.
	 */
	bt_mesh_model_extend(model, srv->onoff.model);
	bt_mesh_model_extend(model, srv->dtt.model);
	bt_mesh_model_extend(
		model,
		bt_mesh_model_find(
			bt_mesh_model_elem(model),
			BT_MESH_MODEL_ID_GEN_POWER_ONOFF_SETUP_SRV));

	if (IS_ENABLED(CONFIG_BT_MESH_SCENE_SRV) &&
	    IS_ENABLED(CONFIG_BT_MESH_PONOFF_SRV_STORED_WITH_SCENE)) {
		bt_mesh_scene_entry_add(model, &srv->scene, &scene_type, false);
	}

	return 0;
}

static void bt_mesh_ponoff_srv_reset(struct bt_mesh_model *model)
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;

	srv->on_power_up = BT_MESH_ON_POWER_UP_OFF;
	net_buf_simple_reset(srv->pub.msg);
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		(void)bt_mesh_model_data_store(srv->ponoff_model, false, NULL,
					       NULL, 0);
	}
}

#ifdef CONFIG_BT_SETTINGS
static int bt_mesh_ponoff_srv_settings_set(struct bt_mesh_model *model,
					   const char *name,
					   size_t len_rd,
					   settings_read_cb read_cb,
					   void *cb_arg)
{
	struct bt_mesh_ponoff_srv *srv = model->user_data;
	struct bt_mesh_onoff_status dummy;
	struct ponoff_settings_data data;
	ssize_t size = MIN(len_rd, sizeof(data));

	if (name) {
		return -ENOENT;
	}

	if (read_cb(cb_arg, &data, size) != size) {
		return -EINVAL;
	}

	set_on_power_up(srv, NULL, (enum bt_mesh_on_power_up)data.on_power_up);

	/* Models that extend Generic Power OnOff Server and, which states are
	 * bound with Generic OnOff state, store the value of the bound state
	 * separately, therefore they don't need to set Generic OnOff state.
	 */
	if (bt_mesh_model_is_extended(model)) {
		return 0;
	}

	struct bt_mesh_model_transition transition = { 0 };
	struct bt_mesh_onoff_set onoff_set = { .transition = &transition };

	switch (data.on_power_up) {
	case BT_MESH_ON_POWER_UP_OFF:
		onoff_set.on_off = false;
		break;
	case BT_MESH_ON_POWER_UP_ON:
		onoff_set.on_off = true;
		break;
	case BT_MESH_ON_POWER_UP_RESTORE:
		onoff_set.on_off = data.on_off;
		break;
	default:
		return -EINVAL;
	}

	srv->onoff.handlers->set(&srv->onoff, NULL, &onoff_set, &dummy);

	return 0;
}
#endif

const struct bt_mesh_model_cb _bt_mesh_ponoff_srv_cb = {
	.init = bt_mesh_ponoff_srv_init,
	.reset = bt_mesh_ponoff_srv_reset,
#ifdef CONFIG_BT_SETTINGS
	.settings_set = bt_mesh_ponoff_srv_settings_set,
#endif
};

void bt_mesh_ponoff_srv_set(struct bt_mesh_ponoff_srv *srv,
			    enum bt_mesh_on_power_up on_power_up)
{
	set_on_power_up(srv, NULL, on_power_up);

	(void)bt_mesh_ponoff_srv_pub(srv, NULL);
}

int bt_mesh_ponoff_srv_pub(struct bt_mesh_ponoff_srv *srv,
			   struct bt_mesh_msg_ctx *ctx)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_PONOFF_OP_STATUS,
				 BT_MESH_PONOFF_MSG_LEN_STATUS);
	bt_mesh_model_msg_init(&msg, BT_MESH_PONOFF_OP_STATUS);
	net_buf_simple_add_u8(&msg, srv->on_power_up);

	return model_send(srv->ponoff_model, ctx, &msg);
}
