/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include "nsim_wifi_if.h"


#include "umac_sim.h"

LOG_MODULE_DECLARE(nrf71_sim, CONFIG_WIFI_NRF71_SIM_LOG_LEVEL);

static const uint8_t golden_new_wiphy[] = {
#include "new_wiphy.bin.inc"
};

static void msg_hdr_fill(struct host_rpu_msg *msg, size_t len, int type)
{
	msg->hdr.len = len;
	msg->hdr.resubmit = 0;
	msg->type = type;
}

/* Send a UMAC event that starts with umac_hdr. Header ids and seq come from the command. */
void umac_evt_send(const struct nrf_wifi_umac_hdr *cmd_hdr, unsigned int evt_id,
			  void *evt, size_t evt_len, uint32_t delay_ms)
{
	uint8_t buf[sizeof(struct host_rpu_msg) + evt_len];
	struct host_rpu_msg *msg = (struct host_rpu_msg *)buf;
	struct nrf_wifi_umac_hdr *hdr = evt;

	memset(hdr, 0, sizeof(*hdr));
	if (cmd_hdr) {
		hdr->seq = cmd_hdr->seq;
		hdr->ids = cmd_hdr->ids;
	}
	hdr->cmd_evnt = evt_id;

	msg_hdr_fill(msg, sizeof(buf), NRF_WIFI_HOST_RPU_MSG_TYPE_UMAC);
	memcpy(msg->msg, evt, evt_len);

	if (delay_ms) {
		umac_sim_evt_send_delayed(buf, sizeof(buf), delay_ms);
	} else {
		umac_sim_evt_send(buf, sizeof(buf));
	}
}

void cmd_status_send(const struct nrf_wifi_umac_hdr *cmd_hdr, unsigned int status)
{
	struct nrf_wifi_umac_event_cmd_status evt = {
		.cmd_id = cmd_hdr->cmd_evnt,
		.cmd_status = status,
	};

	umac_evt_send(cmd_hdr, NRF_WIFI_UMAC_EVENT_CMD_STATUS, &evt, sizeof(evt), 0);
}

static void sys_msg_handle(const struct host_rpu_msg *msg)
{
	const struct nrf_wifi_sys_head *head = (const void *)msg->msg;

	LOG_DBG("SYS cmd %u", head->cmd_event);

	if (head->cmd_event == NRF_WIFI_CMD_INIT) {
		struct {
			struct host_rpu_msg msg;
			struct nrf_wifi_sys_head head;
		} __packed evt = { 0 };

		msg_hdr_fill(&evt.msg, sizeof(evt), NRF_WIFI_HOST_RPU_MSG_TYPE_SYSTEM);
		evt.head.cmd_event = NRF_WIFI_EVENT_INIT_DONE;
		evt.head.len = sizeof(evt.head);
		umac_sim_evt_send(&evt, sizeof(evt));
	}
}

bool umac_sim_host_ready;

void umac_data_carrier_send(unsigned int wdev_id, bool on)
{
	struct {
		struct host_rpu_msg msg;
		struct nrf_wifi_data_carrier_state state;
	} __packed evt = { 0 };

	msg_hdr_fill(&evt.msg, sizeof(evt), NRF_WIFI_HOST_RPU_MSG_TYPE_DATA);
	evt.state.umac_head.cmd = on ? NRF_WIFI_CMD_CARRIER_ON : NRF_WIFI_CMD_CARRIER_OFF;
	evt.state.umac_head.len = sizeof(evt.state);
	evt.state.wdev_id = wdev_id;
	umac_sim_evt_send(&evt, sizeof(evt));
}

static void ifflags_handle(const struct nrf_wifi_umac_hdr *hdr)
{
	const struct nrf_wifi_umac_cmd_chg_vif_state *cmd = (const void *)hdr;
	struct nrf_wifi_umac_event_vif_state evt = { .status = 0 };

	LOG_DBG("SET_IFFLAGS state %d", cmd->info.state);
	cmd_status_send(hdr, 0);
	umac_data_carrier_send(cmd->info.if_index, false);
	umac_evt_send(hdr, NRF_WIFI_UMAC_EVENT_IFFLAGS_STATUS, &evt, sizeof(evt), 0);
}

static void get_wiphy_handle(const struct nrf_wifi_umac_hdr *hdr)
{
	uint8_t buf[sizeof(golden_new_wiphy)];
	struct nrf_wifi_umac_hdr *evt_hdr = (void *)&buf[sizeof(struct host_rpu_msg)];

	memcpy(buf, golden_new_wiphy, sizeof(buf));
	evt_hdr->seq = hdr->seq;
	evt_hdr->ids = hdr->ids;
	umac_sim_evt_send(buf, sizeof(buf));
	cmd_status_send(hdr, 0);
}

static void umac_msg_handle(const struct host_rpu_msg *msg, size_t len)
{
	const struct nrf_wifi_umac_hdr *hdr = (const void *)msg->msg;

	LOG_DBG("UMAC cmd %u len %zu", hdr->cmd_evnt, len);

	switch (hdr->cmd_evnt) {
	case NRF_WIFI_UMAC_CMD_SET_IFFLAGS:
		ifflags_handle(hdr);
		break;
	case NRF_WIFI_UMAC_CMD_GET_WIPHY:
		get_wiphy_handle(hdr);
		break;
	case NRF_WIFI_UMAC_CMD_TRIGGER_SCAN:
		umac_scan_trigger(hdr);
		break;
	case NRF_WIFI_UMAC_CMD_CONFIG_RX_BUF:
		umac_data_rx_buf_cfg(hdr);
		break;
	case NRF_WIFI_UMAC_CMD_AUTHENTICATE:
	case NRF_WIFI_UMAC_CMD_ASSOCIATE:
	case NRF_WIFI_UMAC_CMD_DEAUTHENTICATE:
	case NRF_WIFI_UMAC_CMD_NEW_KEY:
	case NRF_WIFI_UMAC_CMD_SET_KEY:
	case NRF_WIFI_UMAC_CMD_DEL_KEY:
	case NRF_WIFI_UMAC_CMD_GET_STATION:
	case NRF_WIFI_UMAC_CMD_GET_INTERFACE:
	case NRF_WIFI_UMAC_CMD_GET_CONNECTION_INFO:
		umac_conn_cmd(hdr, len);
		break;
	default:
		cmd_status_send(hdr, 0);
		break;
	}
}

static void data_msg_handle(const struct host_rpu_msg *msg, size_t len)
{
	const struct nrf_wifi_umac_head *head = (const void *)msg->msg;

	LOG_DBG("DATA cmd %u len %zu", head->cmd, len);

	if (head->cmd == NRF_WIFI_CMD_TX_BUFF) {
		umac_data_tx((const void *)head);
	}
}

void umac_sim_handle_msg(const uint8_t *data, size_t len)
{
	const struct host_rpu_msg *msg = (const void *)data;

	switch (msg->type) {
	case NRF_WIFI_HOST_RPU_MSG_TYPE_SYSTEM:
		sys_msg_handle(msg);
		break;
	case NRF_WIFI_HOST_RPU_MSG_TYPE_UMAC:
		umac_msg_handle(msg, len);
		break;
	case NRF_WIFI_HOST_RPU_MSG_TYPE_DATA:
		data_msg_handle(msg, len);
		break;
	default:
		LOG_WRN("Unknown message type %d", msg->type);
		break;
	}
}

#define POLL_MS 5

static void poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_handler);

static void poll_handler(struct k_work *work)
{
	static uint8_t frame[NRF_WIFI_MAX_FRAME_LEN];
	int frame_len;
	int evt;

	while ((evt = nsim_wifi_evt_poll(frame, sizeof(frame), &frame_len)) != NSIM_WIFI_EVT_NONE) {
		if (evt == NSIM_WIFI_EVT_SCAN_DONE || evt == NSIM_WIFI_EVT_SCAN_ABORTED) {
			umac_scan_host_evt(evt);
		} else {
			umac_conn_host_evt(evt, frame, frame_len);
		}
	}

	umac_data_rx_poll();
	k_work_schedule_for_queue(&umac_sim_wq, &poll_work, K_MSEC(POLL_MS));
}

static int umac_host_init(void)
{
	int ret = nsim_wifi_open(NULL);

	if (ret) {
		LOG_ERR("No hwsim STA (run hwsim_setup.sh up, give zephyr.exe CAP_NET_ADMIN and "
			"CAP_NET_RAW): %d", ret);
		return 0;
	}

	umac_sim_host_ready = true;
	k_work_schedule_for_queue(&umac_sim_wq, &poll_work, K_MSEC(POLL_MS));

	return 0;
}

SYS_INIT(umac_host_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
