/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**
 * @file
 * @defgroup bt_mesh_chat
 * @{
 * @brief API for the Chat model.
 */

#ifndef BT_MESH_CHAT_H__
#define BT_MESH_CHAT_H__

#include <bluetooth/mesh.h>
#include <bluetooth/mesh/model_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_BT_MESH_CHAT_MESSAGE_BUFFER_SIZE
#define CONFIG_BT_MESH_CHAT_MESSAGE_BUFFER_SIZE 0
#endif

#define BT_MESH_VENDOR_MODEL_ID_CHAT             0x0042
#define BT_MESH_VENDOR_COMPANY_ID                0x0059 // FIXME: Use CONFIG_BT_COMPANY_ID

/** @cond INTERNAL_HIDDEN */
#define BT_MESH_CHAT_OP_MESSAGE BT_MESH_MODEL_OP_3(0xD1, BT_MESH_VENDOR_COMPANY_ID)
#define BT_MESH_CHAT_OP_MESSAGE_REPLY BT_MESH_MODEL_OP_3(0xD2, BT_MESH_VENDOR_COMPANY_ID)
#define BT_MESH_CHAT_OP_PRESENCE BT_MESH_MODEL_OP_3(0xD3, BT_MESH_VENDOR_COMPANY_ID)

#define BT_MESH_CHAT_MSG_MINLEN_MESSAGE 0
#define BT_MESH_CHAT_MSG_MAXLEN_MESSAGE CONFIG_BT_MESH_CHAT_MESSAGE_BUFFER_SIZE
#define BT_MESH_CHAT_MSG_LEN_MESSAGE_REPLY 0
#define BT_MESH_CHAT_MSG_LEN_PRESENCE 1
/** @endcond */

/** Mesh Chat presence state values. */
enum bt_mesh_chat_presence_state
{
    BT_MESH_CHAT_PRESENCE_STATE_AVAILABLE,
    BT_MESH_CHAT_PRESENCE_STATE_AWAY,
    BT_MESH_CHAT_PRESENCE_STATE_INACTIVE,
    BT_MESH_CHAT_PRESENCE_STATE_DO_NOT_DISTURB
};

struct bt_mesh_chat_presence {
	enum bt_mesh_chat_presence_state presence;
};

struct bt_mesh_chat_message {
	uint8_t *msg;
};

struct bt_mesh_chat;

/** @def BT_MESH_CHAT_INIT
 *
 * @brief Init parameters for a @ref bt_mesh_chat instance.
 *
 * @param[in] _handlers State access handlers to use in the model instance.
 */
#define BT_MESH_CHAT_INIT(_handlers)                                           \
	{                                                                      \
		.presence = BT_MESH_CHAT_PRESENCE_STATE_AVAILABLE,             \
		.handlers = _handlers,                                         \
		.pub = {                                                       \
			.update = _bt_mesh_chat_update_handler,                \
			.msg = NET_BUF_SIMPLE(BT_MESH_MODEL_BUF_LEN(           \
				BT_MESH_CHAT_OP_MESSAGE,                       \
				BT_MESH_CHAT_MSG_MAXLEN_MESSAGE)),             \
		},                                                             \
	}

/** @def BT_MESH_MODEL_ONOFF_SRV
 *
 * @brief Generic OnOff Server model composition data entry.
 *
 * @param[in] _srv Pointer to a @ref bt_mesh_chat instance.
 */
#define BT_MESH_MODEL_CHAT(_srv)                                          \
		BT_MESH_MODEL_VND_CB(CONFIG_BT_COMPANY_ID, \
			BT_MESH_VENDOR_MODEL_ID_CHAT,                       \
			_bt_mesh_chat_op, &(_srv)->pub,                  \
			 BT_MESH_MODEL_USER_DATA(struct bt_mesh_chat,     \
						 _srv),                        \
			 &_bt_mesh_chat_cb)

/** Generic OnOff Server state access handlers. */
struct bt_mesh_chat_handlers {
	void (*const presence)(struct bt_mesh_chat *chat,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_chat_presence *rsp);

	void (*const message)(struct bt_mesh_chat *chat,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_chat_message *rsp);

	void (*const message_reply)(struct bt_mesh_chat *chat,
			  struct bt_mesh_msg_ctx *ctx);
};

/**
 * Mesh Chat instance. Should primarily be initialized with the
 * @ref BT_MESH_CHAT_INIT macro.
 */
struct bt_mesh_chat {
	/** Transaction ID tracker. */
	struct bt_mesh_tid_ctx prev_transaction;
	/** Current Transaction ID. */
	uint8_t tid;
	/** Handler function structure. */
	const struct bt_mesh_chat_handlers *handlers;
	/** Response context for tracking acknowledged messages. */
	struct bt_mesh_model_ack_ctx ack_ctx;
	/** Access model pointer. */
	struct bt_mesh_model *model;
	/** Publish parameters. */
	struct bt_mesh_model_pub pub;
	/** Current presence state. */
	enum bt_mesh_chat_presence_state presence;
};

int bt_mesh_chat_presence_pub(struct bt_mesh_chat *chat,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_chat_presence *pres);

int bt_mesh_chat_message_pub(struct bt_mesh_chat *chat,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_chat_message *msg,
			  bool ack);


/** @cond INTERNAL_HIDDEN */
extern const struct bt_mesh_model_op _bt_mesh_chat_op[];
extern const struct bt_mesh_model_cb _bt_mesh_chat_cb;
int _bt_mesh_chat_update_handler(struct bt_mesh_model *model);
/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* BT_MESH_CHAT_H__ */

/** @} */
