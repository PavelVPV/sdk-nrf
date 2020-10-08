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

static void encode_presence(struct net_buf_simple *buf, enum bt_mesh_chat_presence_state presence)
{
	bt_mesh_model_msg_init(buf, BT_MESH_CHAT_OP_PRESENCE);
	net_buf_simple_add_u8(buf, presence);
}

static void decode_presence(struct net_buf_simple_buf *buf, struct bt_mesh_chat_presence *presence)
{
	presence->stat = net_buf_simple_pull_u8(buf);
}

static void message_reply_send(struct bt_mesh_chat *chat, struct bt_mesh_msg_ctx *ctx)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_CHAT_OP_MESSAGE_REPLY,
				 BT_MESH_CHAT_MSG_LEN_MESSAGE_REPLY);
	bt_mesh_model_msg_init(&msg, BT_MESH_CHAT_OP_MESSAGE_REPLY);

	(void)bt_mesh_model_send(chat->model, ctx, &msg);
}

static void handle_message(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{
	struct bt_mesh_chat *chat = model->user_data;
	struct bt_mesh_chat_message msg;
	uint8_t tid;

	tid = net_buf_simple_pull_u8(buf);

	// FIXME: Should probably be uint8_t msg[];
	msg.msg = net_buf_simple_pull_mem(buf, buf->len);

	if (tid_check_and_update(&chat->prev_transaction, tid, ctx) != 0) {
		/* If this is the same transaction, we don't need to send it
		 * to the app, but we still have to respond with a status.
		 */
		goto respond;
	}

	if (model_ack_match(&chat->ack_ctx, BT_MESH_CHAT_OP_MESSAGE_REPLY, ctx)) {
		/* FIXME: Add a comment. Something like: Data needs to be copied. */
		/* TODO: Need another message to show user_data. */
		model_ack_rx(&chat->ack_ctx);
	}

	if (chat->handlers->message) {
		chat->handlers->message(chat, ctx, &msg);
	}

respond:
	message_reply_send(chat, ctx);
}

static void handle_message_reply(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{
	struct bt_mesh_chat *chat = model->user_data;

	if (chat->handlers->message_reply) {
		chat->handlers->message_reply(chat, ctx);
	}
}

static void handle_presence(struct bt_mesh_model *model,
			     struct bt_mesh_msg_ctx *ctx,
			     struct net_buf_simple *buf)
{
	struct bt_mesh_chat *chat = model->user_data;
	struct bt_mesh_chat_presence presence;

	decode_presence(chat, &presence);

	if (chat->handlers->presence) {
		chat->handlers->presence(chat, ctx, &presence);
	}
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
	model_ack_init(&chat->ack_ctx);

	return 0;
}

const struct bt_mesh_model_cb _bt_mesh_chat_cb = {
	.init = bt_mesh_chat_init
};

int _bt_mesh_chat_update_handler(struct bt_mesh_model *model)
{
	struct bt_mesh_chat *chat = model->user_data;

	/* Continue publishing current presence. */
	encode_presence(model->pub->msg, chat->presence);

	return 0;
}

int bt_mesh_chat_presence_pub(struct bt_mesh_chat *chat,
			      struct bt_mesh_msg_ctx *ctx,
			      const struct bt_mesh_chat_presence *pres)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_CHAT_OP_PRESENCE,
				 BT_MESH_CHAT_MSG_LEN_PRESENCE);

	chat->presence = pres->stat;
	encode_presence(&msg, chat->presence);

	return model_send(chat->model, ctx, &msg);
}

int bt_mesh_chat_message_pub(struct bt_mesh_chat *chat,
			     struct bt_mesh_msg_ctx *ctx,
			     const struct bt_mesh_chat_message *msg,
			     bool ack)
{
	BT_MESH_MODEL_BUF_DEFINE(msg, BT_MESH_CHAT_OP_MESSAGE,
				 BT_MESH_CHAT_MSG_MAXLEN_MESSAGE);
	bt_mesh_model_msg_init(&msg, BT_MESH_CHAT_OP_MESSAGE);

	net_buf_simple_add_u8(&msg, chat->tid++);
	net_buf_simple_add_mem(&msg, msg->msg, strnlen(msg->msg, BT_MESH_CHAT_MSG_MAXLEN_MESSAGE));

	return model_ackd_send(chat->model, ctx, &msg,
			       ack ? &chat->ack_ctx : NULL,
			       BT_MESH_CHAT_OP_MESSAGE_REPLY, NULL);
}
