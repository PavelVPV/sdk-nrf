/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Host side of the nRF71 UMAC sim: a small nl80211 client for a mac80211_hwsim STA.
 * Built into the native_simulator runner with the host libc.
 */

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>

#include "nsim_wifi_if.h"

#define NL_BUF_SIZE 16384

static int cmd_sock = -1;
static int evt_sock = -1;
static int pkt_sock = -1;
static int family_id;
static int ifindex;
static uint32_t seq;

struct nl_msg {
	uint8_t buf[2048];
	struct nlmsghdr *nlh;
};

static void msg_init(struct nl_msg *m, int type, int flags, int cmd)
{
	struct genlmsghdr *genl;

	memset(m->buf, 0, sizeof(m->buf));
	m->nlh = (struct nlmsghdr *)m->buf;
	m->nlh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	m->nlh->nlmsg_type = type;
	m->nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	m->nlh->nlmsg_seq = ++seq;
	genl = NLMSG_DATA(m->nlh);
	genl->cmd = cmd;
	genl->version = 1;
}

static void attr_put(struct nl_msg *m, int type, const void *data, int len)
{
	struct nlattr *a = (struct nlattr *)(m->buf + NLMSG_ALIGN(m->nlh->nlmsg_len));

	a->nla_type = type;
	a->nla_len = NLA_HDRLEN + len;
	if (len) {
		memcpy((uint8_t *)a + NLA_HDRLEN, data, len);
	}
	m->nlh->nlmsg_len = NLMSG_ALIGN(m->nlh->nlmsg_len) + NLA_ALIGN(a->nla_len);
}

static void attr_put_u32(struct nl_msg *m, int type, uint32_t v)
{
	attr_put(m, type, &v, sizeof(v));
}

static void attrs_parse(struct nlattr *tb[], int max, void *data, int len)
{
	struct nlattr *a = data;

	memset(tb, 0, sizeof(*tb) * (max + 1));
	while (len >= NLA_HDRLEN && a->nla_len >= NLA_HDRLEN && a->nla_len <= len) {
		int type = a->nla_type & NLA_TYPE_MASK;

		if (type <= max) {
			tb[type] = a;
		}
		len -= NLA_ALIGN(a->nla_len);
		a = (struct nlattr *)((uint8_t *)a + NLA_ALIGN(a->nla_len));
	}
}

static void *attr_data(struct nlattr *a)
{
	return (uint8_t *)a + NLA_HDRLEN;
}

static int attr_len(struct nlattr *a)
{
	return a->nla_len - NLA_HDRLEN;
}

static void genl_attrs(struct nlmsghdr *nlh, struct nlattr *tb[], int max)
{
	struct genlmsghdr *genl = NLMSG_DATA(nlh);

	attrs_parse(tb, max, (uint8_t *)genl + GENL_HDRLEN,
		    nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN));
}

typedef int (*msg_cb_t)(struct nlmsghdr *nlh, void *arg);

/* Send a request and read replies until ACK, error or dump end. */
static int nl_transact(struct nl_msg *m, msg_cb_t cb, void *arg)
{
	static uint8_t rx[NL_BUF_SIZE];
	uint32_t my_seq = m->nlh->nlmsg_seq;

	if (send(cmd_sock, m->buf, m->nlh->nlmsg_len, 0) < 0) {
		return -errno;
	}

	for (;;) {
		int len = recv(cmd_sock, rx, sizeof(rx), 0);
		struct nlmsghdr *nlh;

		if (len < 0) {
			return -errno;
		}

		for (nlh = (struct nlmsghdr *)rx; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
			if (nlh->nlmsg_seq != my_seq) {
				continue;
			}
			if (nlh->nlmsg_type == NLMSG_ERROR) {
				return ((struct nlmsgerr *)NLMSG_DATA(nlh))->error;
			}
			if (nlh->nlmsg_type == NLMSG_DONE) {
				return 0;
			}
			if (cb) {
				cb(nlh, arg);
			}
		}
	}
}

struct family_info {
	int id;
	int grp_scan;
	int grp_mlme;
};

static int family_cb(struct nlmsghdr *nlh, void *arg)
{
	struct family_info *fi = arg;
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	struct nlattr *grp;
	int rem;

	genl_attrs(nlh, tb, CTRL_ATTR_MAX);
	if (tb[CTRL_ATTR_FAMILY_ID]) {
		fi->id = *(uint16_t *)attr_data(tb[CTRL_ATTR_FAMILY_ID]);
	}
	if (!tb[CTRL_ATTR_MCAST_GROUPS]) {
		return 0;
	}

	grp = attr_data(tb[CTRL_ATTR_MCAST_GROUPS]);
	rem = attr_len(tb[CTRL_ATTR_MCAST_GROUPS]);
	while (rem >= NLA_HDRLEN && grp->nla_len >= NLA_HDRLEN) {
		struct nlattr *g[CTRL_ATTR_MCAST_GRP_MAX + 1];

		attrs_parse(g, CTRL_ATTR_MCAST_GRP_MAX, attr_data(grp), attr_len(grp));
		if (g[CTRL_ATTR_MCAST_GRP_NAME] && g[CTRL_ATTR_MCAST_GRP_ID]) {
			const char *name = attr_data(g[CTRL_ATTR_MCAST_GRP_NAME]);
			int id = *(uint32_t *)attr_data(g[CTRL_ATTR_MCAST_GRP_ID]);

			if (!strcmp(name, "scan")) {
				fi->grp_scan = id;
			} else if (!strcmp(name, "mlme")) {
				fi->grp_mlme = id;
			}
		}
		rem -= NLA_ALIGN(grp->nla_len);
		grp = (struct nlattr *)((uint8_t *)grp + NLA_ALIGN(grp->nla_len));
	}

	return 0;
}

static int nl_sock_open(void)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);

	if (s < 0) {
		return -errno;
	}
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(s);
		return -errno;
	}

	return s;
}

int nsim_wifi_open(const char *ifname)
{
	struct family_info fi = { 0 };
	struct nl_msg m;
	int ret;

	char name[IF_NAMESIZE] = { 0 };

	if (!ifname) {
		/* Written by hwsim_setup.sh. NRF71_SIM_STA_IF overrides it. */
		FILE *f = fopen("/tmp/nrf71-sim/sta_if", "r");

		ifname = getenv("NRF71_SIM_STA_IF");
		if (!ifname && f && fscanf(f, "%15s", name) == 1) {
			ifname = name;
		}
		if (f) {
			fclose(f);
		}
		if (!ifname) {
			return -ENODEV;
		}
	}

	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		return -ENODEV;
	}

	cmd_sock = nl_sock_open();
	evt_sock = nl_sock_open();
	if (cmd_sock < 0 || evt_sock < 0) {
		return -EIO;
	}

	msg_init(&m, GENL_ID_CTRL, 0, CTRL_CMD_GETFAMILY);
	attr_put(&m, CTRL_ATTR_FAMILY_NAME, "nl80211", sizeof("nl80211"));
	ret = nl_transact(&m, family_cb, &fi);
	if (ret || !fi.id) {
		return ret ? ret : -ENOENT;
	}
	family_id = fi.id;

	setsockopt(evt_sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &fi.grp_scan, sizeof(int));
	setsockopt(evt_sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &fi.grp_mlme, sizeof(int));
	fcntl(evt_sock, F_SETFL, O_NONBLOCK);

	/* Drop a link left by an earlier run. */
	msg_init(&m, family_id, 0, NL80211_CMD_DISCONNECT);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	(void)nl_transact(&m, NULL, NULL);

	pkt_sock = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_ALL));
	if (pkt_sock >= 0) {
		struct sockaddr_ll sll = {
			.sll_family = AF_PACKET,
			.sll_protocol = htons(ETH_P_ALL),
			.sll_ifindex = ifindex,
		};

		if (bind(pkt_sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
			close(pkt_sock);
			pkt_sock = -1;
		}
	}

	return pkt_sock < 0 ? -EPERM : 0;
}

static int mlme_req(int cmd, const uint8_t *bssid, uint32_t freq, const uint8_t *ssid,
		    int ssid_len, const uint8_t *ie, int ie_len)
{
	struct nl_msg m;

	msg_init(&m, family_id, 0, cmd);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	attr_put(&m, NL80211_ATTR_MAC, bssid, 6);
	attr_put_u32(&m, NL80211_ATTR_WIPHY_FREQ, freq);
	attr_put(&m, NL80211_ATTR_SSID, ssid, ssid_len);
	if (cmd == NL80211_CMD_AUTHENTICATE) {
		attr_put_u32(&m, NL80211_ATTR_AUTH_TYPE, NL80211_AUTHTYPE_OPEN_SYSTEM);
	}
	if (ie_len > 0) {
		attr_put(&m, NL80211_ATTR_IE, ie, ie_len);
	}
	if (cmd == NL80211_CMD_ASSOCIATE) {
		/* Kernel drops the link when zephyr.exe exits. */
		attr_put(&m, NL80211_ATTR_SOCKET_OWNER, NULL, 0);
	}

	return nl_transact(&m, NULL, NULL);
}

int nsim_wifi_auth(const uint8_t *bssid, uint32_t freq, const uint8_t *ssid, int ssid_len,
		   const uint8_t *ie, int ie_len)
{
	return mlme_req(NL80211_CMD_AUTHENTICATE, bssid, freq, ssid, ssid_len, ie, ie_len);
}

int nsim_wifi_assoc(const uint8_t *bssid, uint32_t freq, const uint8_t *ssid, int ssid_len,
		    const uint8_t *ie, int ie_len)
{
	return mlme_req(NL80211_CMD_ASSOCIATE, bssid, freq, ssid, ssid_len, ie, ie_len);
}

int nsim_wifi_deauth(const uint8_t *bssid, uint16_t reason)
{
	struct nl_msg m;

	msg_init(&m, family_id, 0, NL80211_CMD_DEAUTHENTICATE);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	attr_put(&m, NL80211_ATTR_MAC, bssid, 6);
	attr_put(&m, NL80211_ATTR_REASON_CODE, &reason, sizeof(reason));

	return nl_transact(&m, NULL, NULL);
}

int nsim_wifi_key_new(const uint8_t *mac, int idx, uint32_t cipher, const uint8_t *key,
		      int key_len, const uint8_t *seq, int seq_len)
{
	struct nl_msg m;
	uint8_t key_idx = idx;

	msg_init(&m, family_id, 0, NL80211_CMD_NEW_KEY);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	attr_put(&m, NL80211_ATTR_KEY_DATA, key, key_len);
	attr_put(&m, NL80211_ATTR_KEY_IDX, &key_idx, sizeof(key_idx));
	attr_put_u32(&m, NL80211_ATTR_KEY_CIPHER, cipher);
	if (seq_len > 0) {
		attr_put(&m, NL80211_ATTR_KEY_SEQ, seq, seq_len);
	}
	if (mac) {
		attr_put(&m, NL80211_ATTR_MAC, mac, 6);
		attr_put_u32(&m, NL80211_ATTR_KEY_TYPE, NL80211_KEYTYPE_PAIRWISE);
	} else {
		attr_put_u32(&m, NL80211_ATTR_KEY_TYPE, NL80211_KEYTYPE_GROUP);
	}

	return nl_transact(&m, NULL, NULL);
}

int nsim_wifi_key_default(int idx, int unicast, int multicast)
{
	struct nl_msg m;
	uint8_t key_idx = idx;
	uint8_t nest[2 * NLA_HDRLEN];
	int nest_len = 0;

	msg_init(&m, family_id, 0, NL80211_CMD_SET_KEY);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	attr_put(&m, NL80211_ATTR_KEY_IDX, &key_idx, sizeof(key_idx));
	attr_put(&m, NL80211_ATTR_KEY_DEFAULT, NULL, 0);

	/* Nested flags in NL80211_ATTR_KEY_DEFAULT_TYPES. */
	if (unicast) {
		struct nlattr *a = (struct nlattr *)&nest[nest_len];

		a->nla_type = NL80211_KEY_DEFAULT_TYPE_UNICAST;
		a->nla_len = NLA_HDRLEN;
		nest_len += NLA_HDRLEN;
	}
	if (multicast) {
		struct nlattr *a = (struct nlattr *)&nest[nest_len];

		a->nla_type = NL80211_KEY_DEFAULT_TYPE_MULTICAST;
		a->nla_len = NLA_HDRLEN;
		nest_len += NLA_HDRLEN;
	}
	if (nest_len) {
		attr_put(&m, NL80211_ATTR_KEY_DEFAULT_TYPES, nest, nest_len);
	}

	return nl_transact(&m, NULL, NULL);
}

int nsim_wifi_key_del(const uint8_t *mac, int idx)
{
	struct nl_msg m;
	uint8_t key_idx = idx;

	msg_init(&m, family_id, 0, NL80211_CMD_DEL_KEY);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	attr_put(&m, NL80211_ATTR_KEY_IDX, &key_idx, sizeof(key_idx));
	if (mac) {
		attr_put(&m, NL80211_ATTR_MAC, mac, 6);
	}

	return nl_transact(&m, NULL, NULL);
}

int nsim_wifi_pkt_send(const uint8_t *frame, int len)
{
	return send(pkt_sock, frame, len, 0) < 0 ? -errno : 0;
}

int nsim_wifi_pkt_recv(uint8_t *frame, int max)
{
	for (;;) {
		struct sockaddr_ll sll;
		socklen_t sl = sizeof(sll);
		int len = recvfrom(pkt_sock, frame, max, MSG_DONTWAIT,
				   (struct sockaddr *)&sll, &sl);

		if (len <= 0) {
			return 0;
		}
		/* Skip our own TX, seen again on the socket. */
		if (sll.sll_pkttype != PACKET_OUTGOING) {
			return len;
		}
	}
}

int nsim_wifi_scan_trigger(const uint8_t *ssid, int ssid_len)
{
	struct nl_msg m;
	uint8_t nest[64];
	struct nlattr *a = (struct nlattr *)nest;

	msg_init(&m, family_id, 0, NL80211_CMD_TRIGGER_SCAN);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);

	/* NL80211_ATTR_SCAN_SSIDS: nested list, entry 1 = ssid (len 0 = wildcard). */
	if (ssid_len < 0 || ssid_len > 32) {
		ssid_len = 0;
	}
	a->nla_type = 1;
	a->nla_len = NLA_HDRLEN + ssid_len;
	if (ssid_len) {
		memcpy(nest + NLA_HDRLEN, ssid, ssid_len);
	}
	attr_put(&m, NL80211_ATTR_SCAN_SSIDS, nest, NLA_ALIGN(a->nla_len));

	return nl_transact(&m, NULL, NULL);
}

struct scan_ctx {
	struct nsim_wifi_bss *bss;
	int max;
	int cnt;
};

static int scan_cb(struct nlmsghdr *nlh, void *arg)
{
	struct scan_ctx *ctx = arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlattr *b[NL80211_BSS_MAX + 1];
	struct nsim_wifi_bss *e;

	genl_attrs(nlh, tb, NL80211_ATTR_MAX);
	if (!tb[NL80211_ATTR_BSS] || ctx->cnt >= ctx->max) {
		return 0;
	}
	attrs_parse(b, NL80211_BSS_MAX, attr_data(tb[NL80211_ATTR_BSS]),
		    attr_len(tb[NL80211_ATTR_BSS]));
	if (!b[NL80211_BSS_BSSID]) {
		return 0;
	}

	e = &ctx->bss[ctx->cnt++];
	memset(e, 0, sizeof(*e));
	memcpy(e->bssid, attr_data(b[NL80211_BSS_BSSID]), 6);
	if (b[NL80211_BSS_FREQUENCY]) {
		e->freq = *(uint32_t *)attr_data(b[NL80211_BSS_FREQUENCY]);
	}
	if (b[NL80211_BSS_SIGNAL_MBM]) {
		e->signal_mbm = *(int32_t *)attr_data(b[NL80211_BSS_SIGNAL_MBM]);
	}
	if (b[NL80211_BSS_BEACON_INTERVAL]) {
		e->beacon_int = *(uint16_t *)attr_data(b[NL80211_BSS_BEACON_INTERVAL]);
	}
	if (b[NL80211_BSS_CAPABILITY]) {
		e->capab = *(uint16_t *)attr_data(b[NL80211_BSS_CAPABILITY]);
	}
	if (b[NL80211_BSS_TSF]) {
		memcpy(&e->tsf, attr_data(b[NL80211_BSS_TSF]), sizeof(e->tsf));
	}
	if (b[NL80211_BSS_INFORMATION_ELEMENTS]) {
		int len = attr_len(b[NL80211_BSS_INFORMATION_ELEMENTS]);

		e->ies_len = len < NSIM_WIFI_MAX_IES ? len : NSIM_WIFI_MAX_IES;
		memcpy(e->ies, attr_data(b[NL80211_BSS_INFORMATION_ELEMENTS]), e->ies_len);
	}

	return 0;
}

int nsim_wifi_scan_get(struct nsim_wifi_bss *bss, int max)
{
	struct scan_ctx ctx = { .bss = bss, .max = max };
	struct nl_msg m;
	int ret;

	msg_init(&m, family_id, NLM_F_DUMP, NL80211_CMD_GET_SCAN);
	attr_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);
	ret = nl_transact(&m, scan_cb, &ctx);

	return ret < 0 ? ret : ctx.cnt;
}

int nsim_wifi_evt_poll(uint8_t *frame, int max, int *frame_len)
{
	static uint8_t rx[NL_BUF_SIZE];
	static int rx_len;
	static struct nlmsghdr *next;

	*frame_len = 0;

	for (;;) {
		struct nlattr *tb[NL80211_ATTR_MAX + 1];
		struct nlmsghdr *nlh;
		struct genlmsghdr *genl;

		if (!next || !NLMSG_OK(next, rx_len)) {
			rx_len = recv(evt_sock, rx, sizeof(rx), MSG_DONTWAIT);
			if (rx_len <= 0) {
				next = NULL;
				return NSIM_WIFI_EVT_NONE;
			}
			next = (struct nlmsghdr *)rx;
		}

		nlh = next;
		next = NLMSG_NEXT(next, rx_len);
		if (nlh->nlmsg_type != family_id) {
			continue;
		}

		genl = NLMSG_DATA(nlh);
		genl_attrs(nlh, tb, NL80211_ATTR_MAX);
		if (!tb[NL80211_ATTR_IFINDEX] ||
		    *(uint32_t *)attr_data(tb[NL80211_ATTR_IFINDEX]) != (uint32_t)ifindex) {
			continue;
		}

		if (tb[NL80211_ATTR_FRAME] && frame) {
			int len = attr_len(tb[NL80211_ATTR_FRAME]);

			*frame_len = len < max ? len : max;
			memcpy(frame, attr_data(tb[NL80211_ATTR_FRAME]), *frame_len);
		}

		switch (genl->cmd) {
		case NL80211_CMD_NEW_SCAN_RESULTS:
			return NSIM_WIFI_EVT_SCAN_DONE;
		case NL80211_CMD_SCAN_ABORTED:
			return NSIM_WIFI_EVT_SCAN_ABORTED;
		case NL80211_CMD_AUTHENTICATE:
		case NL80211_CMD_ASSOCIATE:
			if (tb[NL80211_ATTR_TIMED_OUT]) {
				return NSIM_WIFI_EVT_TIMEOUT;
			}
			return genl->cmd == NL80211_CMD_AUTHENTICATE ? NSIM_WIFI_EVT_AUTH :
								       NSIM_WIFI_EVT_ASSOC;
		case NL80211_CMD_DEAUTHENTICATE:
			return NSIM_WIFI_EVT_DEAUTH;
		case NL80211_CMD_DISASSOCIATE:
			return NSIM_WIFI_EVT_DISASSOC;
		default:
			break;
		}
	}
}
