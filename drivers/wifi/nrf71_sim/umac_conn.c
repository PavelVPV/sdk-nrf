/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "nsim_wifi_if.h"
#include "umac_sim.h"

LOG_MODULE_DECLARE(nrf71_sim, CONFIG_WIFI_NRF71_SIM_LOG_LEVEL);

static struct {
	struct nrf_wifi_umac_hdr hdr;
	uint8_t bssid[6];
	uint32_t freq;
	bool connected;
	bool local_deauth;
	uint16_t req_ie_len;
	uint8_t req_ie[NRF_WIFI_MAX_IE_LEN];
} conn;

static void mlme_evt_send(unsigned int evt_id, const uint8_t *frame, int frame_len,
			  unsigned int valid_fields, bool with_req_ie)
{
	size_t req_len = with_req_ie ? conn.req_ie_len : 0;
	uint8_t buf[sizeof(struct nrf_wifi_umac_event_mlme) + req_len];
	struct nrf_wifi_umac_event_mlme *evt = (void *)buf;

	memset(buf, 0, sizeof(buf));
	evt->valid_fields = valid_fields;
	evt->frequency = conn.freq;
	evt->frame.frame_len = MIN(frame_len, NRF_WIFI_MAX_FRAME_LEN);
	memcpy(evt->frame.frame, frame, evt->frame.frame_len);
	memcpy(evt->mac_addr, conn.bssid, sizeof(evt->mac_addr));
	evt->req_ie_len = req_len;
	memcpy(evt->req_ie, conn.req_ie, req_len);
	umac_evt_send(&conn.hdr, evt_id, evt, sizeof(buf), 0);
}

static void station_evt_send(unsigned int evt_id)
{
	struct nrf_wifi_umac_event_new_station evt = { 0 };

	evt.wme = 1;
	memcpy(evt.mac_addr, conn.bssid, sizeof(evt.mac_addr));
	umac_evt_send(&conn.hdr, evt_id, &evt, sizeof(evt), 0);
}

static void link_down(void)
{
	if (!conn.connected) {
		return;
	}

	conn.connected = false;
	station_evt_send(NRF_WIFI_UMAC_EVENT_DEL_STATION);
	umac_data_carrier_send(conn.hdr.ids.wdev_id, false);
}

void umac_conn_cmd(const struct nrf_wifi_umac_hdr *hdr, size_t len)
{
	int ret = -ENODEV;

	conn.hdr = *hdr;

	switch (hdr->cmd_evnt) {
	case NRF_WIFI_UMAC_CMD_AUTHENTICATE: {
		const struct nrf_wifi_umac_auth_info *a =
			&((const struct nrf_wifi_umac_cmd_auth *)hdr)->info;

		memcpy(conn.bssid, a->nrf_wifi_bssid, sizeof(conn.bssid));
		conn.freq = a->frequency;
		if (umac_sim_host_ready) {
			ret = nsim_wifi_auth(a->nrf_wifi_bssid, a->frequency, a->ssid.nrf_wifi_ssid,
					     a->ssid.nrf_wifi_ssid_len, (const uint8_t *)a->ie.ie,
					     a->ie.ie_len);
		}
		LOG_INF("AUTHENTICATE %02x:%02x:%02x:%02x:%02x:%02x freq %u: %d",
			a->nrf_wifi_bssid[0], a->nrf_wifi_bssid[1], a->nrf_wifi_bssid[2],
			a->nrf_wifi_bssid[3], a->nrf_wifi_bssid[4], a->nrf_wifi_bssid[5],
			a->frequency, ret);
		break;
	}
	case NRF_WIFI_UMAC_CMD_ASSOCIATE: {
		const struct nrf_wifi_connect_common_info *c =
			&((const struct nrf_wifi_umac_cmd_assoc *)hdr)->connect_common_info;

		conn.req_ie_len = MIN(c->wpa_ie.ie_len, NRF_WIFI_MAX_IE_LEN);
		memcpy(conn.req_ie, c->wpa_ie.ie, conn.req_ie_len);
		if (umac_sim_host_ready) {
			ret = nsim_wifi_assoc(c->mac_addr, c->frequency, c->ssid.nrf_wifi_ssid,
					      c->ssid.nrf_wifi_ssid_len, conn.req_ie,
					      conn.req_ie_len);
		}
		LOG_INF("ASSOCIATE freq %u ie_len %u: %d", c->frequency, conn.req_ie_len, ret);
		break;
	}
	case NRF_WIFI_UMAC_CMD_DEAUTHENTICATE: {
		const struct nrf_wifi_umac_disconn_info *d =
			&((const struct nrf_wifi_umac_cmd_disconn *)hdr)->info;

		conn.local_deauth = true;
		if (umac_sim_host_ready) {
			ret = nsim_wifi_deauth(conn.bssid, d->reason_code);
		}
		LOG_INF("DEAUTHENTICATE reason %u: %d", d->reason_code, ret);
		break;
	}
	case NRF_WIFI_UMAC_CMD_NEW_KEY:
	case NRF_WIFI_UMAC_CMD_DEL_KEY: {
		const struct nrf_wifi_umac_cmd_key *k = (const void *)hdr;
		const struct nrf_wifi_umac_key_info *ki = &k->key_info;
		const uint8_t *mac = (k->valid_fields & NRF_WIFI_CMD_KEY_MAC_ADDR_VALID) ?
				     k->mac_addr : NULL;

		if (!umac_sim_host_ready) {
			break;
		}
		if (hdr->cmd_evnt == NRF_WIFI_UMAC_CMD_DEL_KEY) {
			/* Keys are often deleted when none is set. Errors do not matter. */
			(void)nsim_wifi_key_del(mac, ki->key_idx);
			ret = 0;
			break;
		}
		ret = nsim_wifi_key_new(mac, ki->key_idx, ki->cipher_suite,
					ki->key.nrf_wifi_key, ki->key.nrf_wifi_key_len,
					ki->seq.nrf_wifi_seq,
					(ki->valid_fields & NRF_WIFI_SEQ_VALID) ?
					ki->seq.nrf_wifi_seq_len : 0);
		LOG_INF("NEW_KEY %s idx %u cipher 0x%08x len %u: %d", mac ? "pairwise" : "group",
			ki->key_idx, ki->cipher_suite, ki->key.nrf_wifi_key_len, ret);
		break;
	}
	case NRF_WIFI_UMAC_CMD_SET_KEY: {
		const struct nrf_wifi_umac_key_info *ki =
			&((const struct nrf_wifi_umac_cmd_set_key *)hdr)->key_info;

		if (umac_sim_host_ready) {
			uint16_t flags = ki->nrf_wifi_flags;
			int uc = !!(flags & NRF_WIFI_KEY_DEFAULT_TYPE_UNICAST);
			int mc = !!(flags & NRF_WIFI_KEY_DEFAULT_TYPE_MULTICAST);

			ret = nsim_wifi_key_default(ki->key_idx, uc, mc);
		}
		LOG_INF("SET_KEY idx %u flags 0x%x: %d", ki->key_idx, ki->nrf_wifi_flags, ret);
		/* mac80211 sets the default pairwise key by itself and may refuse this. */
		ret = 0;
		break;
	}
	default:
		break;
	}

	cmd_status_send(hdr, ret ? 1 : 0);
}

void umac_conn_host_evt(int evt, const uint8_t *frame, int frame_len)
{
	LOG_DBG("Host MLME event %d, frame %d B", evt, frame_len);

	switch (evt) {
	case NSIM_WIFI_EVT_AUTH:
		mlme_evt_send(NRF_WIFI_UMAC_EVENT_AUTHENTICATE, frame, frame_len, 0, false);
		break;
	case NSIM_WIFI_EVT_ASSOC: {
		/* Assoc response: status code at offset 26 (24 B header + capab). */
		uint16_t status = (frame_len >= 28) ? sys_get_le16(&frame[26]) : 1;

		if (status == 0) {
			conn.connected = true;
			station_evt_send(NRF_WIFI_UMAC_EVENT_NEW_STATION);
			umac_data_carrier_send(conn.hdr.ids.wdev_id, true);
		}
		mlme_evt_send(NRF_WIFI_UMAC_EVENT_ASSOCIATE, frame, frame_len, 0, true);
		LOG_INF("Associated, status %u", status);
		break;
	}
	case NSIM_WIFI_EVT_DEAUTH:
	case NSIM_WIFI_EVT_DISASSOC:
		link_down();
		mlme_evt_send(evt == NSIM_WIFI_EVT_DEAUTH ? NRF_WIFI_UMAC_EVENT_DEAUTHENTICATE :
							    NRF_WIFI_UMAC_EVENT_DISASSOCIATE,
			      frame, frame_len,
			      conn.local_deauth ? 0 : NRF_WIFI_EVENT_MLME_RXDEAUTH_FROM_AP, false);
		conn.local_deauth = false;
		break;
	case NSIM_WIFI_EVT_TIMEOUT:
		/* No timeout event in this interface. The supplicant times out by itself. */
		LOG_WRN("Auth/assoc timed out");
		break;
	default:
		break;
	}
}
