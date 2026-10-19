/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dns_resolve.h>
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
#define STATE_ALL  (STATE_IP | STATE_DNS)
#define STATE_BITS 3

/* Connectivity state shared by all contexts. Without readiness tracking, always ready. */
static uint32_t state = IS_ENABLED(CONFIG_CONNECT_BY_NAME_READINESS) ||
			IS_ENABLED(CONFIG_CONNECT_BY_NAME_MODEM_READINESS) ? 0 : STATE_ALL;
/* Number of times each state bit came up */
static uint32_t gain_count[STATE_BITS];
static K_MUTEX_DEFINE(state_lock);
static K_CONDVAR_DEFINE(state_changed);

static uint32_t family_state(int family)
{
	if (IS_ENABLED(CONFIG_NET_IPV4) && family == NET_AF_INET) {
		return STATE_IPV4;
	}
	if (IS_ENABLED(CONFIG_NET_IPV6) && family == NET_AF_INET6) {
		return STATE_IPV6;
	}

	return 0;
}

/* Called with state_lock held */
static uint32_t gain_sum_locked(uint32_t mask)
{
	uint32_t sum = 0;

	for (int i = 0; i < STATE_BITS; i++) {
		if (mask & BIT(i)) {
			sum += gain_count[i];
		}
	}

	return sum;
}

#if defined(CONFIG_CONNECT_BY_NAME_READINESS) || defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
/* Called with state_lock held */
static void state_set_locked(uint32_t new_state)
{
	uint32_t gained = new_state & ~state;

	if (new_state == state) {
		return;
	}

	for (int i = 0; i < STATE_BITS; i++) {
		if (gained & BIT(i)) {
			gain_count[i]++;
		}
	}

	LOG_DBG("IPv4 %d, IPv6 %d, DNS %d", !!(new_state & STATE_IPV4),
		!!(new_state & STATE_IPV6), !!(new_state & STATE_DNS));

	state = new_state;
	k_condvar_broadcast(&state_changed);
}
#endif

#if defined(CONFIG_CONNECT_BY_NAME_READINESS)
#define EVENT_MASK (NET_EVENT_L4_IPV4_CONNECTED | NET_EVENT_L4_IPV4_DISCONNECTED | \
		    NET_EVENT_L4_IPV6_CONNECTED | NET_EVENT_L4_IPV6_DISCONNECTED | \
		    NET_EVENT_L4_DNS_READY | NET_EVENT_L4_DNS_LOST)

static void event_handler(uint64_t event, struct net_if *iface, void *info, size_t info_length,
			  void *user_data)
{
	uint32_t new_state;

	k_mutex_lock(&state_lock, K_FOREVER);
	new_state = state;

	switch (event) {
	case NET_EVENT_L4_IPV4_CONNECTED:
		new_state |= STATE_IPV4;
		break;
	case NET_EVENT_L4_IPV4_DISCONNECTED:
		new_state &= ~STATE_IPV4;
		break;
	case NET_EVENT_L4_IPV6_CONNECTED:
		new_state |= STATE_IPV6;
		break;
	case NET_EVENT_L4_IPV6_DISCONNECTED:
		new_state &= ~STATE_IPV6;
		break;
	case NET_EVENT_L4_DNS_READY:
		new_state |= STATE_DNS;
		break;
	case NET_EVENT_L4_DNS_LOST:
		new_state &= ~STATE_DNS;
		break;
	default:
		break;
	}

	state_set_locked(new_state);
	k_mutex_unlock(&state_lock);
}

/* Registered statically to not miss events raised before the first call */
NET_MGMT_REGISTER_EVENT_HANDLER(connect_by_name_events, EVENT_MASK, event_handler, NULL);
#endif /* CONFIG_CONNECT_BY_NAME_READINESS */

#if defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
/* The modem resolves names itself, so DNS is ready when an IP family is up */
static uint32_t modem_state(void)
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

/* Sleeps for the poll interval, or until the deadline if sooner */
static void poll_sleep(k_timepoint_t deadline)
{
	k_timepoint_t poll = sys_timepoint_calc(K_MSEC(CONFIG_CONNECT_BY_NAME_MODEM_POLL_INTERVAL_MS));

	if (sys_timepoint_cmp(deadline, poll) < 0) {
		poll = deadline;
	}

	k_sleep(sys_timepoint_timeout(poll));
}
#endif /* CONFIG_CONNECT_BY_NAME_MODEM_READINESS */

#if !defined(CONFIG_CONNECT_BY_NAME_READINESS)
/* Gets the state bits of mask that are up, and the sum of their gain counters */
static void snapshot(uint32_t mask, uint32_t *up, uint32_t *gains)
{
#if defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
	uint32_t polled = modem_state();
#endif

	k_mutex_lock(&state_lock, K_FOREVER);
#if defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
	state_set_locked(polled);
#endif
	*up = state & mask;
	*gains = gain_sum_locked(mask);
	k_mutex_unlock(&state_lock);
}
#endif

/* True if a wanted family is up and, if needed, DNS is available */
static bool is_ready(uint32_t up, uint32_t mask)
{
	return (up & STATE_IP) && ((up & STATE_DNS) || !(mask & STATE_DNS));
}

/* Waits up to @p wait for the state to be ready for @p mask. Always sets @p up and @p gains. */
static int wait_ready(uint32_t mask, k_timeout_t wait, uint32_t *up, uint32_t *gains)
{
	k_timepoint_t deadline = sys_timepoint_calc(wait);

#if defined(CONFIG_CONNECT_BY_NAME_READINESS)
	int err = 0;

	k_mutex_lock(&state_lock, K_FOREVER);
	while (!is_ready(state & mask, mask)) {
		if (k_condvar_wait(&state_changed, &state_lock, sys_timepoint_timeout(deadline))) {
			err = -ENETDOWN;
			break;
		}
	}
	*up = state & mask;
	*gains = gain_sum_locked(mask);
	k_mutex_unlock(&state_lock);

	return err;
#else
	while (true) {
		snapshot(mask, up, gains);
		if (is_ready(*up, mask)) {
			return 0;
		}

		if (sys_timepoint_expired(deadline)) {
			return -ENETDOWN;
		}

#if defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
		poll_sleep(deadline);
#endif
	}
#endif /* CONFIG_CONNECT_BY_NAME_READINESS */
}

static bool is_numeric(const char *host)
{
	uint8_t buf[sizeof(struct net_in6_addr)];

	return zsock_inet_pton(NET_AF_INET, host, buf) == 1 ||
	       zsock_inet_pton(NET_AF_INET6, host, buf) == 1;
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

static int dns_err(int err)
{
	switch (err) {
	case DNS_EAI_AGAIN:
		return -EAGAIN;
	case DNS_EAI_MEMORY:
		return -ENOMEM;
	default:
		return -EHOSTUNREACH;
	}
}

int connect_by_name_resolve(struct connect_by_name *ctx, const char *host, const char *service,
			    const struct zsock_addrinfo *hints, uint16_t port, k_timeout_t wait)
{
	struct zsock_addrinfo h = {0};
	struct zsock_addrinfo *res;
	uint32_t mask;
	uint32_t up;
	int count = 0;
	int err;

	if (ctx == NULL || host == NULL) {
		return -EINVAL;
	}

	if (hints != NULL) {
		h.ai_family = hints->ai_family;
		h.ai_socktype = hints->ai_socktype;
		h.ai_protocol = hints->ai_protocol;
		h.ai_flags = hints->ai_flags;
	}

	connect_by_name_free(ctx);

	mask = (h.ai_family == NET_AF_UNSPEC) ?
	       (family_state(NET_AF_INET) | family_state(NET_AF_INET6)) :
	       family_state(h.ai_family);
	if (mask == 0) {
		return -EINVAL;
	}

	if (!is_numeric(host)) {
		mask |= STATE_DNS;
	}

	ctx->mask = mask;
	ctx->port = port;

	err = wait_ready(mask, wait, &up, &ctx->gains);
	if (err) {
		return err;
	}

	/* If only one family is up, only query that one: avoids returning unreachable
	 * addresses and saves a DNS round trip.
	 */
	if ((up & STATE_IP) == STATE_IPV4) {
		h.ai_family = NET_AF_INET;
	} else if ((up & STATE_IP) == STATE_IPV6) {
		h.ai_family = NET_AF_INET6;
	}

	err = zsock_getaddrinfo(host, service, &h, &res);
	if (err) {
		LOG_WRN("Failed to resolve %s: %d", host, err);
		return dns_err(err);
	}

	for (struct zsock_addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
		if (family_state(ai->ai_family)) {
			count++;
		}
	}

	if (count == 0) {
		LOG_WRN("No usable address for %s", host);
		zsock_freeaddrinfo(res);
		return -EHOSTUNREACH;
	}

	LOG_DBG("Resolved %s: %d address(es)", host, count);

	ctx->res = res;
	ctx->next = res;

	return 0;
}

int connect_by_name_next(struct connect_by_name *ctx, const struct net_sockaddr **addr,
			 net_socklen_t *addrlen)
{
	uint32_t up;

	if (ctx == NULL || addr == NULL || addrlen == NULL) {
		return -EINVAL;
	}

	/* Last known state, the modem is not polled for each address */
	k_mutex_lock(&state_lock, K_FOREVER);
	up = state;
	k_mutex_unlock(&state_lock);

	while (ctx->next != NULL) {
		struct zsock_addrinfo *ai = ctx->next;
		struct net_sockaddr *sa = ai->ai_addr;
		char ip[NET_IPV6_ADDR_LEN] = "";

		ctx->next = ai->ai_next;

		/* Not wanted, or down */
		if (!(up & ctx->mask & family_state(ai->ai_family))) {
			continue;
		}

		if (ctx->port != 0) {
			if (sa->sa_family == NET_AF_INET6) {
				net_sin6(sa)->sin6_port = net_htons(ctx->port);
			} else {
				net_sin(sa)->sin_port = net_htons(ctx->port);
			}
		}

		addr_str(sa, ip, sizeof(ip));
		LOG_INF("Address %s, port %u", ip,
			net_ntohs(sa->sa_family == NET_AF_INET6 ? net_sin6(sa)->sin6_port :
								  net_sin(sa)->sin_port));

		*addr = sa;
		*addrlen = addr_len(sa);

		return 0;
	}

	return -ENOENT;
}

void connect_by_name_free(struct connect_by_name *ctx)
{
	if (ctx == NULL) {
		return;
	}

	if (ctx->res != NULL) {
		zsock_freeaddrinfo(ctx->res);
	}

	ctx->res = NULL;
	ctx->next = NULL;
}

int connect_by_name_wait_change(const struct connect_by_name *ctx, k_timeout_t timeout)
{
	uint32_t mask = (ctx != NULL && ctx->mask != 0) ? ctx->mask : STATE_ALL;
	uint32_t seen = (ctx != NULL) ? ctx->gains : 0;

#if defined(CONFIG_CONNECT_BY_NAME_READINESS)
	k_timepoint_t deadline = sys_timepoint_calc(timeout);
	int err = 0;

	k_mutex_lock(&state_lock, K_FOREVER);
	while (gain_sum_locked(mask) == seen) {
		if (k_condvar_wait(&state_changed, &state_lock, sys_timepoint_timeout(deadline))) {
			err = -EAGAIN;
			break;
		}
	}
	k_mutex_unlock(&state_lock);

	return err;
#elif defined(CONFIG_CONNECT_BY_NAME_MODEM_READINESS)
	k_timepoint_t deadline = sys_timepoint_calc(timeout);
	uint32_t up;
	uint32_t gains;

	while (true) {
		snapshot(mask, &up, &gains);
		if (gains != seen) {
			return 0;
		}

		if (sys_timepoint_expired(deadline)) {
			return -EAGAIN;
		}

		poll_sleep(deadline);
	}
#else
	ARG_UNUSED(mask);
	ARG_UNUSED(seen);

	k_sleep(timeout);

	return -EAGAIN;
#endif
}
