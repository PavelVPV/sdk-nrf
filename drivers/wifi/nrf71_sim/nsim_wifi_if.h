/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Interface between the Zephyr side of the sim and the host (Linux nl80211) side.
 * Only plain C types: the two sides are built with different libc headers.
 */

#ifndef NSIM_WIFI_IF_H__
#define NSIM_WIFI_IF_H__

#include <stdint.h>

#define NSIM_WIFI_MAX_IES 768

struct nsim_wifi_bss {
	uint8_t bssid[6];
	uint16_t beacon_int;
	uint16_t capab;
	uint32_t freq;
	int32_t signal_mbm;
	uint64_t tsf;
	uint32_t ies_len;
	uint8_t ies[NSIM_WIFI_MAX_IES];
};

enum nsim_wifi_evt {
	NSIM_WIFI_EVT_NONE,
	NSIM_WIFI_EVT_SCAN_DONE,
	NSIM_WIFI_EVT_SCAN_ABORTED,
	NSIM_WIFI_EVT_AUTH,
	NSIM_WIFI_EVT_ASSOC,
	NSIM_WIFI_EVT_DEAUTH,
	NSIM_WIFI_EVT_DISASSOC,
	NSIM_WIFI_EVT_TIMEOUT,
};

/* Open nl80211 for the interface (NULL: from hwsim_setup.sh). Returns 0 or a negative errno. */
int nsim_wifi_open(const char *ifname);

/* Start a scan. ssid may be NULL (wildcard). */
int nsim_wifi_scan_trigger(const uint8_t *ssid, int ssid_len);

/* Read the kernel scan results. Returns the number of entries. */
int nsim_wifi_scan_get(struct nsim_wifi_bss *bss, int max);

/* Non-blocking. Returns enum nsim_wifi_evt. frame gets the MLME frame, if any. */
int nsim_wifi_evt_poll(uint8_t *frame, int max, int *frame_len);

/* MLME requests. Results come as events. */
int nsim_wifi_auth(const uint8_t *bssid, uint32_t freq, const uint8_t *ssid, int ssid_len,
		   const uint8_t *ie, int ie_len);
int nsim_wifi_assoc(const uint8_t *bssid, uint32_t freq, const uint8_t *ssid, int ssid_len,
		    const uint8_t *ie, int ie_len);
int nsim_wifi_deauth(const uint8_t *bssid, uint16_t reason);

/* Keys. mac = peer for a pairwise key, NULL for a group key. cipher is the IEEE suite. */
int nsim_wifi_key_new(const uint8_t *mac, int idx, uint32_t cipher, const uint8_t *key,
		      int key_len, const uint8_t *seq, int seq_len);
int nsim_wifi_key_default(int idx, int unicast, int multicast);
int nsim_wifi_key_del(const uint8_t *mac, int idx);

/* Ethernet frames on the STA interface. recv is non-blocking, returns 0 if none. */
int nsim_wifi_pkt_send(const uint8_t *frame, int len);
int nsim_wifi_pkt_recv(uint8_t *frame, int max);

#endif /* NSIM_WIFI_IF_H__ */
