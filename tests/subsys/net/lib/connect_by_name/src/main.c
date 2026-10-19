/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_offload.h>
#include <net/connect_by_name.h>

#define HOST "example.com"
#define PORT 5683

/* Fake DNS records, IPv6 first as returned by the resolver */
static const char *const records[] = { "2001:db8::1", "2001:db8::2", "192.0.2.1" };

static struct {
	int getaddrinfo_calls;
	int last_family;
	int last_flags;
	const char *last_service;
	bool ignore_hints;
	bool fail_resolve;
} fake;

static struct zsock_addrinfo ai_buf[ARRAY_SIZE(records) + 1];
static struct net_sockaddr_storage sa_buf[ARRAY_SIZE(records) + 1];

static bool fill_ai(int idx, const char *addr, int family)
{
	struct net_sockaddr *sa = (struct net_sockaddr *)&sa_buf[idx];
	void *dst = (family == NET_AF_INET6) ? (void *)&net_sin6(sa)->sin6_addr :
					       (void *)&net_sin(sa)->sin_addr;

	memset(sa, 0, sizeof(sa_buf[idx]));
	if (zsock_inet_pton(family, addr, dst) != 1) {
		return false;
	}

	sa->sa_family = family;
	ai_buf[idx] = (struct zsock_addrinfo) {
		.ai_family = family,
		.ai_addr = sa,
		.ai_addrlen = (family == NET_AF_INET6) ? sizeof(struct net_sockaddr_in6) :
							 sizeof(struct net_sockaddr_in),
	};

	return true;
}

static int fake_getaddrinfo(const char *node, const char *service,
			    const struct zsock_addrinfo *hints, struct zsock_addrinfo **res)
{
	int count = 0;

	fake.getaddrinfo_calls++;
	fake.last_family = hints->ai_family;
	fake.last_flags = hints->ai_flags;
	fake.last_service = service;

	if (fake.fail_resolve) {
		return DNS_EAI_FAIL;
	}

	if (fill_ai(0, node, NET_AF_INET) || fill_ai(0, node, NET_AF_INET6)) {
		count = 1;
	} else {
		ARRAY_FOR_EACH(records, i) {
			int family = strchr(records[i], ':') ? NET_AF_INET6 : NET_AF_INET;

			if (fake.ignore_hints || hints->ai_family == NET_AF_UNSPEC ||
			    hints->ai_family == family) {
				fill_ai(count++, records[i], family);
			}
		}
	}

	if (count == 0) {
		return DNS_EAI_NONAME;
	}

	for (int i = 0; i < count - 1; i++) {
		ai_buf[i].ai_next = &ai_buf[i + 1];
	}
	ai_buf[count - 1].ai_next = NULL;
	*res = &ai_buf[0];

	return 0;
}

static void fake_freeaddrinfo(struct zsock_addrinfo *res)
{
}

static const struct socket_dns_offload fake_dns = {
	.getaddrinfo = fake_getaddrinfo,
	.freeaddrinfo = fake_freeaddrinfo,
};

static const struct connect_by_name *ctx;

/* Defines the context of the test in the scope of the test */
#define USE_CTX(...)								\
	struct connect_by_name test_ctx = CONNECT_BY_NAME_INIT(__VA_ARGS__);	\
	ctx = &test_ctx

#define USE_DEFAULT_CTX() USE_CTX(HOST, PORT, NET_SOCK_DGRAM, NET_AF_UNSPEC)

static void notify(uint64_t event)
{
	net_mgmt_event_notify(event, NULL);
	k_msleep(10);
}

static void set_state(bool ipv4, bool ipv6, bool dns)
{
	notify(ipv4 ? NET_EVENT_L4_IPV4_CONNECTED : NET_EVENT_L4_IPV4_DISCONNECTED);
	notify(ipv6 ? NET_EVENT_L4_IPV6_CONNECTED : NET_EVENT_L4_IPV6_DISCONNECTED);
	notify(dns ? NET_EVENT_L4_DNS_READY : NET_EVENT_L4_DNS_LOST);
}

static uint64_t delayed_event;

static void delayed_event_fn(struct k_work *work)
{
	net_mgmt_event_notify(delayed_event, NULL);
}

static K_WORK_DELAYABLE_DEFINE(delayed_work, delayed_event_fn);

static void notify_later(uint64_t event, k_timeout_t delay)
{
	delayed_event = event;
	k_work_schedule(&delayed_work, delay);
}

/* Gets the next address as a string, checks its length and port. Returns the error. */
static int next_wait(char *buf, k_timeout_t wait)
{
	const struct net_sockaddr *sa;
	net_socklen_t len;
	int err = connect_by_name_next(ctx, &sa, &len, wait);

	if (err) {
		return err;
	}

	zassert_equal(len, sa->sa_family == NET_AF_INET6 ? sizeof(struct net_sockaddr_in6) :
							   sizeof(struct net_sockaddr_in));
	zassert_equal(net_ntohs(sa->sa_family == NET_AF_INET6 ? net_sin6(sa)->sin6_port :
								net_sin(sa)->sin_port),
		      PORT);
	zsock_inet_ntop(sa->sa_family,
			sa->sa_family == NET_AF_INET6 ? (const void *)&net_sin6(sa)->sin6_addr :
							(const void *)&net_sin(sa)->sin_addr,
			buf, NET_IPV6_ADDR_LEN);

	return 0;
}

static int next(char *buf)
{
	return next_wait(buf, K_NO_WAIT);
}

static void *suite_setup(void)
{
	socket_offload_dns_register(&fake_dns);
	return NULL;
}

static void before(void *f)
{
	k_work_cancel_delayable(&delayed_work);
	set_state(false, false, false);
	memset(&fake, 0, sizeof(fake));
	ctx = NULL;
}

ZTEST(connect_by_name, test_dual_stack)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	set_state(true, true, true);

	zassert_ok(next(buf));
	zassert_equal(fake.last_family, NET_AF_UNSPEC);
	zassert_str_equal(buf, "2001:db8::1");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(fake.getaddrinfo_calls, 1);

	/* All addresses returned: report it, then start from scratch */
	zassert_equal(next(buf), -ENOENT);
	zassert_equal(fake.getaddrinfo_calls, 1);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
	zassert_equal(fake.getaddrinfo_calls, 2);
}

ZTEST(connect_by_name, test_ipv4_only)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);

	zassert_ok(next(buf));
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

ZTEST(connect_by_name, test_ipv6_only)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(false, true, true);

	zassert_ok(next(buf));
	zassert_equal(fake.last_family, NET_AF_INET6);
	zassert_str_equal(buf, "2001:db8::1");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");
	zassert_equal(next(buf), -ENOENT);
}

/* The configured family limits what is resolved and returned */
ZTEST(connect_by_name, test_configured_family)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_CTX(HOST, PORT, NET_SOCK_DGRAM, NET_AF_INET);
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(next(buf));
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);

	/* The family that is not wanted does not make the network ready */
	set_state(false, true, true);
	zassert_equal(next(buf), -ENETDOWN);
}

/* Resolver returns both families, only addresses of ready families are returned */
ZTEST(connect_by_name, test_skip_not_ready)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);
	fake.ignore_hints = true;

	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

/* The host is resolved again when the families that are up changed */
ZTEST(connect_by_name, test_resolve_on_family_change)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
	zassert_equal(fake.getaddrinfo_calls, 1);

	set_state(true, false, true);

	zassert_ok(next(buf));
	zassert_equal(fake.getaddrinfo_calls, 2);
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_str_equal(buf, "192.0.2.1");

	/* Same families again: keep iterating */
	zassert_equal(next(buf), -ENOENT);
	zassert_equal(fake.getaddrinfo_calls, 2);
}

ZTEST(connect_by_name, test_no_dns)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, false);

	zassert_equal(next(buf), -ENETDOWN);
	zassert_equal(fake.getaddrinfo_calls, 0);
}

ZTEST(connect_by_name, test_numeric_host_without_dns)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	USE_CTX("192.0.2.7", PORT, NET_SOCK_DGRAM, NET_AF_UNSPEC);
	set_state(true, false, false);

	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.7");
	zassert_equal(next(buf), -ENOENT);
}

ZTEST(connect_by_name, test_wait_timeout)
{
	char buf[NET_IPV6_ADDR_LEN];
	int64_t start = k_uptime_get();

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);

	zassert_equal(next_wait(buf, K_MSEC(100)), -ENETDOWN);
	zassert_true(k_uptime_get() - start >= 100);
	zassert_equal(fake.getaddrinfo_calls, 0);
}

ZTEST(connect_by_name, test_wait_for_network)
{
	char buf[NET_IPV6_ADDR_LEN];
	int64_t start = k_uptime_get();

	USE_DEFAULT_CTX();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(false, false, true);
	notify_later(NET_EVENT_L4_IPV4_CONNECTED, K_MSEC(100));

	zassert_ok(next_wait(buf, K_SECONDS(5)));
	zassert_true(k_uptime_get() - start >= 100);
	zassert_true(k_uptime_get() - start < 2000);
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_str_equal(buf, "192.0.2.1");
}

ZTEST(connect_by_name, test_resolve_failure)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	set_state(true, true, true);
	fake.fail_resolve = true;

	zassert_equal(next(buf), -EHOSTUNREACH);

	/* Resolves again on the next call */
	fake.fail_resolve = false;
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
}

ZTEST(connect_by_name, test_reset)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_DEFAULT_CTX();

	set_state(true, true, true);

	zassert_ok(next(buf));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");

	connect_by_name_reset(ctx);

	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
	zassert_equal(fake.getaddrinfo_calls, 2);
}

ZTEST(connect_by_name, test_service_and_flags)
{
	char buf[NET_IPV6_ADDR_LEN];

	USE_CTX(HOST, PORT, NET_SOCK_DGRAM, NET_AF_UNSPEC, .service = "1", .ai_flags = AI_PDNSERV);
	set_state(true, true, true);

	zassert_ok(next(buf));
	zassert_str_equal(fake.last_service, "1");
	zassert_equal(fake.last_flags, AI_PDNSERV);
}

ZTEST(connect_by_name, test_invalid)
{
	USE_DEFAULT_CTX();
	const struct net_sockaddr *sa;
	net_socklen_t len;
	struct connect_by_name no_host = { 0 };

	zassert_equal(connect_by_name_next(NULL, &sa, &len, K_NO_WAIT), -EINVAL);
	zassert_equal(connect_by_name_next(&no_host, &sa, &len, K_NO_WAIT), -EINVAL);
	zassert_equal(connect_by_name_next(ctx, NULL, &len, K_NO_WAIT), -EINVAL);
	zassert_equal(connect_by_name_next(ctx, &sa, NULL, K_NO_WAIT), -EINVAL);
}

ZTEST_SUITE(connect_by_name, NULL, suite_setup, before, NULL, NULL);
