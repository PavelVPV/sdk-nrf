/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include "uart_handler.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(chat, 4);

/* Configuration server */

/** Configuration server definition */
static struct bt_mesh_cfg_srv cfg_srv = {
	.relay = IS_ENABLED(CONFIG_BT_MESH_RELAY),
	.beacon = BT_MESH_BEACON_ENABLED,
	.frnd = IS_ENABLED(CONFIG_BT_MESH_FRIEND),
	.gatt_proxy = IS_ENABLED(CONFIG_BT_MESH_GATT_PROXY),
	.default_ttl = 7,

	/* 3 transmissions with 20ms interval */
	.net_transmit = BT_MESH_TRANSMIT(2, 20),
	.relay_retransmit = BT_MESH_TRANSMIT(2, 20),
};

/* Health server */

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_delayed_work attention_blink_work;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
		BIT(0) | BIT(1),
		BIT(1) | BIT(2),
		BIT(2) | BIT(3),
		BIT(3) | BIT(0),
	};
	dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
	k_delayed_work_submit(&attention_blink_work, K_MSEC(30));
}

static void attention_on(struct bt_mesh_model *mod)
{
	k_delayed_work_submit(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(struct bt_mesh_model *mod)
{
	k_delayed_work_cancel(&attention_blink_work);
	dk_set_leds(DK_NO_LEDS_MSK);
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* Chat model */

static const char * presence_to_string(enum bt_mesh_chat_presence_state state)
{
	const char *str = "unknown";

	switch (state)
	{
		case BT_MESH_CHAT_PRESENCE_STATE_AVAILABLE:
			str = "available";
			break;

		case BT_MESH_CHAT_PRESENCE_STATE_AWAY:
			str = "away";
			break;

		case BT_MESH_CHAT_PRESENCE_STATE_DO_NOT_DISTURB:
			str = "do not disturb";
			break;

		case BT_MESH_CHAT_PRESENCE_STATE_INACTIVE:
			str = "inactive";
			break;
	}

	return str;
}

static void handle_chat_presence(struct bt_mesh_chat *chat,
				 struct bt_mesh_msg_ctx *ctx,
				 struct bt_mesh_chat_presence *rsp)
{
	LOG_INF("---> [%04X] changed presence to: [%s]", ctx->addr, presence_to_string(rsp->presence));
}

static void handle_chat_message(struct bt_mesh_chat *chat,
				struct bt_mesh_msg_ctx *ctx,
				struct bt_mesh_chat_message *rsp)
{
	LOG_INF("[%04X]: %s", ctx->addr, rsp->msg);
	uart_handler_tx(rsp->msg, strnlen(rsp->msg, UINT8_MAX));
}

static void handle_chat_message_reply(struct bt_mesh_chat *chat,
				      struct bt_mesh_msg_ctx *ctx)
{
	LOG_INF("<--- reply received from [%04X]", ctx->addr);
}

static const struct bt_mesh_chat_handlers chat_handlers = {
		.presence = handle_chat_presence,
		.message = handle_chat_message,
		.message_reply = handle_chat_message_reply,
};

static struct bt_mesh_chat chat = BT_MESH_CHAT_INIT(&chat_handlers);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(
		1,
		BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_CFG_SRV(&cfg_srv),
			BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
		BT_MESH_MODEL_LIST(BT_MESH_MODEL_CHAT(&chat))
		),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void button_handler_cb(uint32_t pressed, uint32_t changed)
{
	if (!bt_mesh_is_provisioned()) {
		return;
	}

	for (int i = 0; i < 4; ++i) {
		struct bt_mesh_chat_presence presence;
		int err;

		if (!(pressed & changed & BIT(i))) {
			continue;
		}

		switch (i)
		{
			case 0:
				presence.presence = BT_MESH_CHAT_PRESENCE_STATE_AVAILABLE;
				break;

			case 1:
				presence.presence = BT_MESH_CHAT_PRESENCE_STATE_AWAY;
				break;

			case 2:
				presence.presence = BT_MESH_CHAT_PRESENCE_STATE_DO_NOT_DISTURB;
				break;

			case 3:
				presence.presence = BT_MESH_CHAT_PRESENCE_STATE_INACTIVE;
				break;
		}


		err = bt_mesh_chat_presence_pub(&chat, NULL, &presence);

		if (err) {
			printk("Presence %d sentt failed: %d\n", presence.presence, err);
		}
	}
}

static void uart_handler_rx_callback(const uint8_t * data, uint8_t len)
{
	struct bt_mesh_chat_message msg;
	int err;

#if 0
	msg.msg = k_malloc(len);
	if (!msg.msg) {
		LOG_WRN("Unable to alloc mem");
		return;
	}
	memcpy(msg.msg, data, len);
	k_free(msg.msg);
#else
	msg.msg = data;
#endif

#if 1
	err = bt_mesh_chat_message_pub(&chat, NULL, &msg, false);
	if (err) {
		LOG_WRN("No ACK received");
	}
#endif
}

const struct bt_mesh_comp *model_handler_init(void)
{
	static struct button_handler button_handler = {
		.cb = button_handler_cb,
	};

	dk_button_handler_add(&button_handler);

	k_delayed_work_init(&attention_blink_work, attention_blink);

	int err = uart_handler_init(uart_handler_rx_callback);
	if (err) {
		LOG_WRN("UART init err: %d", err);
	} else {
		LOG_WRN("UART init-ed");
	}

	return &comp;
}
