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

/* GET_STATION, GET_INTERFACE and GET_CONNECTION_INFO. The host waits for one event each. */
static bool status_cmd(const struct nrf_wifi_umac_hdr *hdr)
{
	struct nsim_wifi_sta sta = { 0 };

	if (conn.connected && umac_sim_host_ready) {
		int ret = nsim_wifi_sta_get(conn.bssid, &sta);

		LOG_DBG("GET_STATION: %d, signal %d dBm, tx rate %u", ret, sta.signal_dbm,
			sta.tx_bitrate);
		if (!ret && sta.signal_dbm) {
			umac_sim_rx_signal_mbm = sta.signal_dbm * 100;
		}
	}

	switch (hdr->cmd_evnt) {
	case NRF_WIFI_UMAC_CMD_GET_STATION: {
		struct nrf_wifi_umac_event_new_station evt = { 0 };
		struct nrf_wifi_sta_info *si = &evt.sta_info;

		memcpy(evt.mac_addr, conn.bssid, sizeof(evt.mac_addr));
		if (conn.connected) {
			si->valid_fields = NRF_WIFI_STA_INFO_SIGNAL_VALID |
					   NRF_WIFI_STA_INFO_SIGNAL_AVG_VALID |
					   NRF_WIFI_STA_INFO_RX_BYTES_VALID |
					   NRF_WIFI_STA_INFO_TX_BYTES_VALID |
					   NRF_WIFI_STA_INFO_RX_PACKETS_VALID |
					   NRF_WIFI_STA_INFO_TX_PACKETS_VALID;
			si->signal = sta.signal_dbm;
			si->signal_avg = sta.signal_dbm;
			si->rx_bytes = sta.rx_bytes;
			si->tx_bytes = sta.tx_bytes;
			si->rx_packets = sta.rx_packets;
			si->tx_packets = sta.tx_packets;
			if (sta.tx_bitrate) {
				si->valid_fields |= NRF_WIFI_STA_INFO_TX_BITRATE_VALID;
				si->tx_bitrate.valid_fields = NRF_WIFI_RATE_INFO_BITRATE_VALID;
				si->tx_bitrate.bitrate = sta.tx_bitrate;
			}
		}
		umac_evt_send(hdr, NRF_WIFI_UMAC_EVENT_GET_STATION, &evt, sizeof(evt), 0);
		return true;
	}
	case NRF_WIFI_UMAC_CMD_GET_INTERFACE: {
		struct nrf_wifi_interface_info evt = { 0 };

		evt.nrf_wifi_iftype = NRF_WIFI_IFTYPE_STATION;
		evt.chan_def.chan.center_frequency = conn.freq;
		evt.chan_def.width = NRF_WIFI_CHAN_WIDTH_20;
		evt.chan_def.center_frequency1 = conn.freq;
		umac_evt_send(hdr, NRF_WIFI_UMAC_EVENT_NEW_INTERFACE, &evt, sizeof(evt), 0);
		return true;
	}
	case NRF_WIFI_UMAC_CMD_GET_CONNECTION_INFO: {
		struct nrf_wifi_umac_event_conn_info evt = { 0 };

		evt.associated = conn.connected;
		evt.beacon_interval = sta.beacon_int;
		evt.dtim_interval = sta.dtim_period;
		umac_evt_send(hdr, NRF_WIFI_UMAC_EVENT_GET_CONNECTION_INFO, &evt, sizeof(evt), 0);
		return true;
	}
	default:
		return false;
	}
}

void umac_conn_cmd(const struct nrf_wifi_umac_hdr *hdr, size_t len)
{
	int ret = -ENODEV;

	if (status_cmd(hdr)) {
		cmd_status_send(hdr, 0);
		return;
	}

	conn.hdr = *hdr;

	switch (hdr->cmd_evnt) {
	case NRF_WIFI_UMAC_CMD_AUTHENTICATE: {
		const struct nrf_wifi_umac_auth_info *a =
			&((const struct nrf_wifi_umac_cmd_auth *)hdr)->info;

		memcpy(conn.bssid, a->nrf_wifi_bssid, sizeof(conn.bssid));
		conn.freq = a->frequency;
		bool sae = (a->auth_type == NRF_WIFI_AUTHTYPE_SAE);
		int sae_len = sae ? MIN(a->sae.sae_data_len, NRF_WIFI_MAX_SAE_DATA_LENGTH) : 0;

		if (umac_sim_host_ready) {
			ret = nsim_wifi_auth(a->nrf_wifi_bssid, a->frequency, a->ssid.nrf_wifi_ssid,
					     a->ssid.nrf_wifi_ssid_len, (const uint8_t *)a->ie.ie,
					     a->ie.ie_len,
					     sae ? NSIM_WIFI_AUTH_SAE : NSIM_WIFI_AUTH_OPEN,
					     a->sae.sae_data, sae_len);
		}
		LOG_INF("AUTHENTICATE %02x:%02x:%02x:%02x:%02x:%02x freq %u %s: %d",
			a->nrf_wifi_bssid[0], a->nrf_wifi_bssid[1], a->nrf_wifi_bssid[2],
			a->nrf_wifi_bssid[3], a->nrf_wifi_bssid[4], a->nrf_wifi_bssid[5],
			a->frequency, sae ? "SAE" : "open", ret);
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
					      conn.req_ie_len, c->use_mfp == NRF_WIFI_MFP_REQUIRED);
		}
		LOG_INF("ASSOCIATE freq %u ie_len %u mfp %d: %d", c->frequency, conn.req_ie_len,
			c->use_mfp, ret);
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
			int mgmt = !!(flags & NRF_WIFI_KEY_DEFAULT_MGMT);
			int uc = !!(flags & NRF_WIFI_KEY_DEFAULT_TYPE_UNICAST);
			int mc = !!(flags & NRF_WIFI_KEY_DEFAULT_TYPE_MULTICAST);

			ret = nsim_wifi_key_default(ki->key_idx, mgmt, uc, mc);
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
