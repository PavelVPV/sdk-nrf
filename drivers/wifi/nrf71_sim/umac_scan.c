/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/logging/log.h>

#include "nsim_wifi_if.h"
#include "umac_sim.h"

LOG_MODULE_DECLARE(nrf71_sim, CONFIG_WIFI_NRF71_SIM_LOG_LEVEL);

#define SCAN_MAX_BSS 16

#define IE_SSID 0
#define IE_RSN  48

static struct {
	bool active;
	struct nrf_wifi_umac_hdr hdr;
	struct nrf_wifi_umac_scan_info info;
} scan;

static struct nsim_wifi_bss bss_list[SCAN_MAX_BSS];

static const uint8_t *ie_find(const struct nsim_wifi_bss *b, uint8_t id, uint8_t *len)
{
	const uint8_t *p = b->ies;
	const uint8_t *end = b->ies + b->ies_len;

	while (p + 2 <= end && p + 2 + p[1] <= end) {
		if (p[0] == id) {
			*len = p[1];
			return p + 2;
		}
		p += 2 + p[1];
	}

	return NULL;
}

static unsigned int freq_to_chan(uint32_t freq)
{
	if (freq == 2484) {
		return 14;
	}

	return (freq < 5000) ? (freq - 2407) / 5 : (freq - 5000) / 5;
}

static size_t db_fill_display(uint8_t *db, size_t db_len, int cnt)
{
	struct umac_display_results *r = (void *)db;
	size_t n = 0;

	for (int i = 0; i < cnt && (n + 1) * sizeof(*r) <= db_len; i++, n++) {
		const struct nsim_wifi_bss *b = &bss_list[i];
		const uint8_t *ssid;
		uint8_t len = 0;

		memset(&r[n], 0, sizeof(r[n]));
		ssid = ie_find(b, IE_SSID, &len);
		if (ssid && len <= NRF_WIFI_MAX_SSID_LEN) {
			r[n].ssid.nrf_wifi_ssid_len = len;
			memcpy(r[n].ssid.nrf_wifi_ssid, ssid, len);
		}
		memcpy(r[n].mac_addr, b->bssid, sizeof(r[n].mac_addr));
		r[n].nwk_band = (b->freq < 5000) ? 0 : 1;
		r[n].nwk_channel = freq_to_chan(b->freq);
		r[n].security_type = ie_find(b, IE_RSN, &len) ? NRF_WIFI_WPA2 : NRF_WIFI_OPEN;
		r[n].beacon_interval = b->beacon_int;
		r[n].capability = b->capab;
		r[n].signal.signal_type = NRF_WIFI_SIGNAL_TYPE_MBM;
		r[n].signal.signal.mbm_signal = b->signal_mbm;
	}

	return n;
}

static size_t db_fill_connect(uint8_t *db, size_t db_len, int cnt)
{
	size_t off = 0;
	size_t n = 0;

	for (int i = 0; i < cnt; i++) {
		const struct nsim_wifi_bss *b = &bss_list[i];
		struct nrf_wifi_umac_event_new_scan_results *r = (void *)(db + off);
		size_t entry_len = sizeof(*r) + b->ies_len;

		if (off + entry_len > db_len) {
			break;
		}

		memset(r, 0, sizeof(*r));
		r->valid_fields = NRF_WIFI_EVENT_NEW_SCAN_RESULTS_MAC_ADDR_VALID |
				  NRF_WIFI_EVENT_NEW_SCAN_RESULTS_IES_VALID |
				  NRF_WIFI_EVENT_NEW_SCAN_RESULTS_IES_TSF_VALID |
				  NRF_WIFI_EVENT_NEW_SCAN_RESULTS_BEACON_INTERVAL_VALID |
				  NRF_WIFI_EVENT_NEW_SCAN_RESULTS_SIGNAL_VALID;
		r->frequency = b->freq;
		r->ies_tsf = b->tsf;
		r->beacon_interval = b->beacon_int;
		r->capability = b->capab;
		r->signal.signal_type = NRF_WIFI_SIGNAL_TYPE_MBM;
		r->signal.signal.mbm_signal = b->signal_mbm;
		memcpy(r->mac_addr, b->bssid, sizeof(r->mac_addr));
		r->ies_len = b->ies_len;
		memcpy(r->ies, b->ies, b->ies_len);
		off += entry_len;
		n++;
	}

	return n;
}

static void scan_done_send(int cnt)
{
	struct nrf_wifi_umac_event_scan_done evt = { 0 };
	uint8_t *db = (uint8_t *)scan.info.scan_db_addr;
	size_t n = 0;

	if (cnt > 0 && db) {
		n = (scan.info.scan_reason == SCAN_CONNECT) ?
			db_fill_connect(db, scan.info.scan_db_len, cnt) :
			db_fill_display(db, scan.info.scan_db_len, cnt);
	}

	LOG_INF("Scan done: %d BSS, %zu in DB (reason %d)", cnt, n, scan.info.scan_reason);

	evt.status = 0;
	evt.scan_type = scan.info.scan_reason;
	evt.scan_results_cnt = n;
	evt.scan_db_addr = scan.info.scan_db_addr;
	scan.active = false;
	umac_evt_send(&scan.hdr, NRF_WIFI_UMAC_EVENT_SCAN_DONE, &evt, sizeof(evt), 0);
}

void umac_scan_trigger(const struct nrf_wifi_umac_hdr *hdr)
{
	const struct nrf_wifi_umac_cmd_scan *cmd = (const void *)hdr;
	const struct nrf_wifi_scan_params *p = &cmd->info.scan_params;
	struct nrf_wifi_umac_event_trigger_scan start = { 0 };
	int ret = -ENODEV;

	scan.hdr = *hdr;
	scan.info = cmd->info;
	scan.active = true;

	if (umac_sim_host_ready) {
		const struct nrf_wifi_ssid *ssid = p->num_scan_ssids ? &p->scan_ssids[0] : NULL;

		ret = nsim_wifi_scan_trigger(ssid ? ssid->nrf_wifi_ssid : NULL,
					     ssid ? ssid->nrf_wifi_ssid_len : 0);
	}

	LOG_DBG("TRIGGER_SCAN reason %d db_len %u ssids %u: %d", cmd->info.scan_reason,
		cmd->info.scan_db_len, p->num_scan_ssids, ret);

	umac_evt_send(hdr, NRF_WIFI_UMAC_EVENT_TRIGGER_SCAN_START, &start, sizeof(start), 0);
	cmd_status_send(hdr, 0);

	if (ret) {
		LOG_ERR("Host scan failed: %d", ret);
		scan_done_send(0);
	}
}

void umac_scan_host_evt(int evt)
{
	int cnt = 0;

	if (!scan.active) {
		return;
	}

	if (evt == NSIM_WIFI_EVT_SCAN_DONE) {
		cnt = nsim_wifi_scan_get(bss_list, SCAN_MAX_BSS);
	}

	scan_done_send(cnt < 0 ? 0 : cnt);
}
