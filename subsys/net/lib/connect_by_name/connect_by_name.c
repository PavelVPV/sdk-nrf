/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <net/connect_by_name.h>

#if defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
#include <nrf_modem_at.h>
#endif

LOG_MODULE_REGISTER(connect_by_name, CONFIG_CONNECT_BY_NAME_LOG_LEVEL);

#define STATE_IPV4 BIT(0)
#define STATE_IPV6 BIT(1)
#define STATE_DNS  BIT(2)
#define STATE_IP   (STATE_IPV4 | STATE_IPV6)

#define EVENT_MASK (NET_EVENT_L4_IPV4_CONNECTED | NET_EVENT_L4_IPV4_DISCONNECTED | \
		    NET_EVENT_L4_IPV6_CONNECTED | NET_EVENT_L4_IPV6_DISCONNECTED | \
		    NET_EVENT_L4_DNS_READY | NET_EVENT_L4_DNS_LOST)

static uint32_t family_state(int family)
{
	switch (family) {
	case NET_AF_INET:
		return STATE_IPV4;
	case NET_AF_INET6:
		return STATE_IPV6;
	default:
		return 0;
	}
}

#if defined(CONFIG_CONNECT_BY_NAME_READINESS)
/* Connectivity state shared by all contexts */
static uint32_t state;
static K_MUTEX_DEFINE(state_lock);
static K_CONDVAR_DEFINE(state_changed);

static void event_handler(uint64_t event, struct net_if *iface, void *info, size_t info_length,
			  void *user_data)
{
	uint32_t bit;
	bool up;

	switch (event) {
	case NET_EVENT_L4_IPV4_CONNECTED:
	case NET_EVENT_L4_IPV4_DISCONNECTED:
		bit = STATE_IPV4;
		break;
	case NET_EVENT_L4_IPV6_CONNECTED:
	case NET_EVENT_L4_IPV6_DISCONNECTED:
		bit = STATE_IPV6;
		break;
	case NET_EVENT_L4_DNS_READY:
	case NET_EVENT_L4_DNS_LOST:
		bit = STATE_DNS;
		break;
	default:
		return;
	}

	up = (event == NET_EVENT_L4_IPV4_CONNECTED || event == NET_EVENT_L4_IPV6_CONNECTED ||
	      event == NET_EVENT_L4_DNS_READY);

	k_mutex_lock(&state_lock, K_FOREVER);
	state = up ? (state | bit) : (state & ~bit);
	LOG_DBG("IPv4 %d, IPv6 %d, DNS %d", !!(state & STATE_IPV4), !!(state & STATE_IPV6),
		!!(state & STATE_DNS));
	k_condvar_broadcast(&state_changed);
	k_mutex_unlock(&state_lock);
}

/* Registered statically to not miss events raised before the first call */
NET_MGMT_REGISTER_EVENT_HANDLER(connect_by_name_events, EVENT_MASK, event_handler, NULL);

static uint32_t get_state(void)
{
	uint32_t s;

	k_mutex_lock(&state_lock, K_FOREVER);
	s = state;
	k_mutex_unlock(&state_lock);

	return s;
}
#elif defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
/* The modem resolves names itself, so DNS is ready when an IP family is up */
static uint32_t get_state(void)
{
	char addr1[NET_IPV6_ADDR_LEN] = {0};
	char addr2[NET_IPV6_ADDR_LEN] = {0};
	uint8_t buf[sizeof(struct net_in6_addr)];
	uint32_t up = 0;

	/* The IPv4 address, if any, comes first */
	if (nrf_modem_at_scanf("AT+CGPADDR=0", "+CGPADDR: %*d,\"%46[.:0-9A-F]\",\"%46[:0-9A-F]\"",
			       addr1, addr2) <= 0) {
		return 0;
	}

	if (zsock_inet_pton(NET_AF_INET, addr1, buf) == 1) {
		up |= STATE_IPV4;
	}
	if (zsock_inet_pton(NET_AF_INET6, addr1, buf) == 1 ||
	    zsock_inet_pton(NET_AF_INET6, addr2, buf) == 1) {
		up |= STATE_IPV6;
	}

	return up ? (up | STATE_DNS) : 0;
}
#else
/* Without readiness tracking, always ready */
static uint32_t get_state(void)
{
	return STATE_IP | STATE_DNS;
}
#endif

/* True if a wanted family is up and, if needed, DNS is available */
static bool is_ready(uint32_t up, uint32_t mask)
{
	return (up & STATE_IP) && ((up & STATE_DNS) || !(mask & STATE_DNS));
}

#if defined(CONFIG_CONNECT_BY_NAME_READINESS)
/* Waits up to @p wait for the state to be ready for @p mask. Returns the state in @p up. */
static int wait_ready(uint32_t mask, k_timeout_t wait, uint32_t *up)
{
	k_timepoint_t deadline = sys_timepoint_calc(wait);
	int err = 0;

	k_mutex_lock(&state_lock, K_FOREVER);
	while (!is_ready(state & mask, mask)) {
		if (k_condvar_wait(&state_changed, &state_lock, sys_timepoint_timeout(deadline))) {
			err = -ENETDOWN;
			break;
		}
	}
	*up = state & mask;
	k_mutex_unlock(&state_lock);

	return err;
}
#elif defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
/* No events from the modem, poll it */
static int wait_ready(uint32_t mask, k_timeout_t wait, uint32_t *up)
{
	k_timepoint_t deadline = sys_timepoint_calc(wait);
	k_timeout_t poll = K_MSEC(CONFIG_CONNECT_BY_NAME_MODEM_POLL_INTERVAL_MS);

	while (*up = get_state() & mask, !is_ready(*up, mask)) {
		k_timeout_t left = sys_timepoint_timeout(deadline);

		if (sys_timepoint_expired(deadline)) {
			return -ENETDOWN;
		}

		k_sleep(!K_TIMEOUT_EQ(left, K_FOREVER) && left.ticks < poll.ticks ? left : poll);
	}

	return 0;
}
#else
static int wait_ready(uint32_t mask, k_timeout_t wait, uint32_t *up)
{
	ARG_UNUSED(wait);

	*up = get_state() & mask;

	return 0;
}
#endif /* CONFIG_CONNECT_BY_NAME_READINESS */

static bool is_numeric(const char *host)
{
	uint8_t buf[sizeof(struct net_in6_addr)];

	return zsock_inet_pton(NET_AF_INET, host, buf) == 1 ||
	       zsock_inet_pton(NET_AF_INET6, host, buf) == 1;
}

/* State bits of the families and services the context needs */
static uint32_t wanted(const struct connect_by_name *ctx)
{
	uint32_t mask = ctx->family == NET_AF_UNSPEC ? STATE_IP : family_state(ctx->family);

	return is_numeric(ctx->host) ? mask : (mask | STATE_DNS);
}

static net_socklen_t addr_len(const struct net_sockaddr *sa)
{
	return (sa->sa_family == NET_AF_INET6) ? sizeof(struct net_sockaddr_in6) :
						 sizeof(struct net_sockaddr_in);
}

static const char *addr_str(const struct net_sockaddr *sa, char *buf, size_t len)
{
	const void *addr = (sa->sa_family == NET_AF_INET6) ?
			   (const void *)&net_sin6(sa)->sin6_addr :
			   (const void *)&net_sin(sa)->sin_addr;

	return zsock_inet_ntop(sa->sa_family, addr, buf, len);
}

/* Resolves the host for the families that are up */
static int resolve(const struct connect_by_name *ctx, uint32_t up)
{
	struct connect_by_name_data *data = ctx->data;
	struct zsock_addrinfo *res;
	struct zsock_addrinfo hints = {
		.ai_family = NET_AF_UNSPEC,
		.ai_socktype = ctx->socktype,
		.ai_flags = ctx->ai_flags,
	};
	int err;

	/* If only one family is up, only query that one: avoids filling addrs with
	 * unreachable addresses and saves a DNS round trip.
	 */
	if ((up & STATE_IP) == STATE_IPV4) {
		hints.ai_family = NET_AF_INET;
	} else if ((up & STATE_IP) == STATE_IPV6) {
		hints.ai_family = NET_AF_INET6;
	}

	data->addr_count = 0;
	data->addr_next = 0;
	data->resolved = up & STATE_IP;

	err = zsock_getaddrinfo(ctx->host, ctx->service, &hints, &res);
	if (err) {
		LOG_WRN("Failed to resolve %s: %d", ctx->host, err);
		return -EHOSTUNREACH;
	}

	/* Copy supported results, in the order we got from getaddrinfo() */
	for (struct zsock_addrinfo *ai = res;
	     ai != NULL && data->addr_count < ARRAY_SIZE(data->addrs); ai = ai->ai_next) {
		/* Next free slot */
		struct net_sockaddr *sa = (struct net_sockaddr *)&data->addrs[data->addr_count];

		if (IS_ENABLED(CONFIG_NET_IPV6) && ai->ai_family == NET_AF_INET6) {
			memcpy(sa, ai->ai_addr, sizeof(struct net_sockaddr_in6));
			net_sin6(sa)->sin6_port = net_htons(ctx->port);
		} else if (IS_ENABLED(CONFIG_NET_IPV4) && ai->ai_family == NET_AF_INET) {
			memcpy(sa, ai->ai_addr, sizeof(struct net_sockaddr_in));
			net_sin(sa)->sin_port = net_htons(ctx->port);
		} else {
			/* Unsupported or disabled family: skip, reuse the slot. */
			continue;
		}

		data->addr_count++;
	}

	zsock_freeaddrinfo(res);

	LOG_DBG("Resolved %s: %u address(es)", ctx->host, data->addr_count);

	return data->addr_count > 0 ? 0 : -EHOSTUNREACH;
}

int connect_by_name_next(const struct connect_by_name *ctx, const struct net_sockaddr **addr,
			 net_socklen_t *addrlen, k_timeout_t wait)
{
	struct connect_by_name_data *data;
	uint32_t mask;
	uint32_t up;
	int err;

	if (ctx == NULL || ctx->host == NULL || ctx->data == NULL || addr == NULL ||
	    addrlen == NULL) {
		return -EINVAL;
	}

	data = ctx->data;
	mask = wanted(ctx);

	/* All addresses were returned: start over on the next call */
	if (data->addr_count > 0 && data->addr_next >= data->addr_count) {
		connect_by_name_reset(ctx);
		return -ENOENT;
	}

	/* Resolve again if the families that are up changed */
	up = get_state() & mask;
	if (data->addr_count > 0 && (up & STATE_IP) != data->resolved) {
		connect_by_name_reset(ctx);
	}

	if (data->addr_count == 0) {
		err = wait_ready(mask, wait, &up);
		if (err) {
			return err;
		}

		err = resolve(ctx, up);
		if (err) {
			return err;
		}
	}

	for (; data->addr_next < data->addr_count; data->addr_next++) {
		const struct net_sockaddr *sa =
			(const struct net_sockaddr *)&data->addrs[data->addr_next];
		char ip[NET_IPV6_ADDR_LEN] = "";

		if (!(up & family_state(sa->sa_family))) {
			continue;
		}

		LOG_INF("Address of %s: %s, port %u", ctx->host, addr_str(sa, ip, sizeof(ip)),
			ctx->port);

		data->addr_next++;
		*addr = sa;
		*addrlen = addr_len(sa);

		return 0;
	}

	connect_by_name_reset(ctx);

	return -ENOENT;
}

void connect_by_name_reset(const struct connect_by_name *ctx)
{
	ctx->data->addr_count = 0;
	ctx->data->addr_next = 0;
	ctx->data->resolved = 0;
}
