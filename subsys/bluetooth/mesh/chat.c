/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <bluetooth/mesh.h>
#include <bluetooth/mesh/chat.h>
#include <logging/log.h>
#include "model_utils.h"

LOG_MODULE_REGISTER(bt_mesh_chat, CONFIG_BT_MESH_CHAT_LOG_LEVEL);

static void handle_message(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{

}

static void handle_message_reply(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{

}

static void handle_presence(struct bt_mesh_model *model,
			     struct bt_mesh_msg_ctx *ctx,
			     struct net_buf_simple *buf)
{

}

const struct bt_mesh_model_op _bt_mesh_chat_op[] = {
	{ BT_MESH_CHAT_OP_MESSAGE, BT_MESH_CHAT_MSG_MINLEN_MESSAGE, handle_message },
	{ BT_MESH_CHAT_OP_MESSAGE_REPLY, BT_MESH_CHAT_MSG_LEN_MESSAGE_REPLY, handle_message_reply },
	{ BT_MESH_CHAT_OP_PRESENCE, BT_MESH_CHAT_MSG_LEN_PRESENCE, handle_presence },
	BT_MESH_MODEL_OP_END,
};

static int bt_mesh_chat_init(struct bt_mesh_model *model)
{
	struct bt_mesh_chat *chat = model->user_data;

	chat->model = model;
	net_buf_simple_init(model->pub->msg, 0);

	return 0;
}

const struct bt_mesh_model_cb _bt_mesh_chat_cb = {
	.init = bt_mesh_chat_init
};

int _bt_mesh_chat_update_handler(struct bt_mesh_model *model)
{
	struct bt_mesh_chat *chat = model->user_data;
	struct bt_mesh_onoff_status status = { 0 };



	return 0;
}

int32_t bt_mesh_chat_pub(struct bt_mesh_chat *chat,
			    struct bt_mesh_msg_ctx *ctx,
			    const struct bt_mesh_onoff_status *status)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_ONOFF_OP_STATUS,
				 BT_MESH_ONOFF_MSG_MAXLEN_STATUS);

	return model_send(chat->model, ctx, &msg);
}
