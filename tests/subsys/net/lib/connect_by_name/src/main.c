/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_offload.h>
#include <net/connect_by_name.h>

#define HOST "example.com"
#define PORT 5683
/* Port set by the fake resolver */
#define RESOLVED_PORT 1234

/* Fake DNS records, IPv6 first as returned by the resolver */
static const char *const records[] = { "2001:db8::1", "2001:db8::2", "192.0.2.1" };

static struct {
	int getaddrinfo_calls;
	int freeaddrinfo_calls;
	int last_family;
	int last_socktype;
	int last_flags;
	const char *last_service;
	bool ignore_hints;
	int fail_resolve;
} fake;

static struct zsock_addrinfo ai_buf[ARRAY_SIZE(records) + 1];
static struct net_sockaddr_storage sa_buf[ARRAY_SIZE(records) + 1];

static const struct zsock_addrinfo hints_any = { .ai_socktype = NET_SOCK_DGRAM };

static struct connect_by_name ctx;

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
	if (family == NET_AF_INET6) {
		net_sin6(sa)->sin6_port = net_htons(RESOLVED_PORT);
	} else {
		net_sin(sa)->sin_port = net_htons(RESOLVED_PORT);
	}

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
	fake.last_socktype = hints->ai_socktype;
	fake.last_flags = hints->ai_flags;
	fake.last_service = service;

	if (fake.fail_resolve) {
		return fake.fail_resolve;
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
	zassert_equal(res, &ai_buf[0]);
	fake.freeaddrinfo_calls++;
}

static const struct socket_dns_offload fake_dns = {
	.getaddrinfo = fake_getaddrinfo,
	.freeaddrinfo = fake_freeaddrinfo,
};

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

static int resolve(const char *host, int family, k_timeout_t wait)
{
	struct zsock_addrinfo hints = hints_any;

	hints.ai_family = family;

	return connect_by_name_resolve(&ctx, host, NULL, &hints, PORT, wait);
}

/* Gets the next address as a string, checks its length and port. Returns the error. */
static int next_port(char *buf, uint16_t port)
{
	const struct net_sockaddr *sa;
	net_socklen_t len;
	int err = connect_by_name_next(&ctx, &sa, &len);

	if (err) {
		return err;
	}

	zassert_equal(len, sa->sa_family == NET_AF_INET6 ? sizeof(struct net_sockaddr_in6) :
							   sizeof(struct net_sockaddr_in));
	zassert_equal(net_ntohs(sa->sa_family == NET_AF_INET6 ? net_sin6(sa)->sin6_port :
								net_sin(sa)->sin_port),
		      port);
	zsock_inet_ntop(sa->sa_family,
			sa->sa_family == NET_AF_INET6 ? (const void *)&net_sin6(sa)->sin6_addr :
							(const void *)&net_sin(sa)->sin_addr,
			buf, NET_IPV6_ADDR_LEN);

	return 0;
}

static int next(char *buf)
{
	return next_port(buf, PORT);
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
	memset(&ctx, 0, sizeof(ctx));
}

static void after(void *f)
{
	connect_by_name_free(&ctx);
}

ZTEST(connect_by_name, test_dual_stack)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(fake.last_family, NET_AF_UNSPEC);
	zassert_equal(fake.last_socktype, NET_SOCK_DGRAM);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
	zassert_equal(next(buf), -ENOENT);
	zassert_equal(fake.getaddrinfo_calls, 1);

	connect_by_name_free(&ctx);
	zassert_equal(fake.freeaddrinfo_calls, 1);
}

ZTEST(connect_by_name, test_ipv4_only)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

ZTEST(connect_by_name, test_ipv6_only)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(false, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(fake.last_family, NET_AF_INET6);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");
	zassert_equal(next(buf), -ENOENT);
}

/* The wanted family limits what is resolved */
ZTEST(connect_by_name, test_wanted_family)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_INET, K_NO_WAIT));
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);

	/* The family that is not wanted does not make the network ready */
	set_state(false, true, true);
	zassert_equal(resolve(HOST, NET_AF_INET, K_NO_WAIT), -ENETDOWN);
	zassert_equal(fake.getaddrinfo_calls, 1);
}

/* Resolver returns both families, only addresses of families that are up are returned */
ZTEST(connect_by_name, test_skip_not_ready)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);
	fake.ignore_hints = true;

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

/* Addresses of a family that is not wanted are skipped, even if the resolver returns them */
ZTEST(connect_by_name, test_skip_not_wanted)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);
	fake.ignore_hints = true;

	zassert_ok(resolve(HOST, NET_AF_INET, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

/* A family that goes down after resolving is skipped */
ZTEST(connect_by_name, test_skip_lost_family)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");

	set_state(true, false, true);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
	zassert_equal(next(buf), -ENOENT);
}

ZTEST(connect_by_name, test_no_dns)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, false);

	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT), -ENETDOWN);
	zassert_equal(fake.getaddrinfo_calls, 0);
}

ZTEST(connect_by_name, test_numeric_host_without_dns)
{
	char buf[NET_IPV6_ADDR_LEN];

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, false);

	zassert_ok(resolve("192.0.2.7", NET_AF_UNSPEC, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.7");
	zassert_equal(next(buf), -ENOENT);
}

ZTEST(connect_by_name, test_wait_timeout)
{
	int64_t start = k_uptime_get();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);

	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_MSEC(100)), -ENETDOWN);
	zassert_true(k_uptime_get() - start >= 100);
	zassert_equal(fake.getaddrinfo_calls, 0);
}

ZTEST(connect_by_name, test_wait_for_network)
{
	char buf[NET_IPV6_ADDR_LEN];
	int64_t start = k_uptime_get();

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(false, false, true);
	notify_later(NET_EVENT_L4_IPV4_CONNECTED, K_MSEC(100));

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_SECONDS(5)));
	zassert_true(k_uptime_get() - start >= 100);
	zassert_true(k_uptime_get() - start < 2000);
	zassert_equal(fake.last_family, NET_AF_INET);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "192.0.2.1");
}

ZTEST(connect_by_name, test_resolve_errors)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	fake.fail_resolve = DNS_EAI_FAIL;
	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT), -EHOSTUNREACH);
	fake.fail_resolve = DNS_EAI_NONAME;
	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT), -EHOSTUNREACH);
	fake.fail_resolve = DNS_EAI_AGAIN;
	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT), -EAGAIN);
	fake.fail_resolve = DNS_EAI_MEMORY;
	zassert_equal(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT), -ENOMEM);

	/* Nothing to iterate or free after a failure */
	zassert_equal(next(buf), -ENOENT);
	connect_by_name_free(&ctx);
	zassert_equal(fake.freeaddrinfo_calls, 0);

	fake.fail_resolve = 0;
	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");
}

/* Resolving again frees the previous result and starts over */
ZTEST(connect_by_name, test_resolve_again)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_ok(next(buf));
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::2");

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(fake.freeaddrinfo_calls, 1);
	zassert_ok(next(buf));
	zassert_str_equal(buf, "2001:db8::1");

	connect_by_name_free(&ctx);
	connect_by_name_free(&ctx);
	zassert_equal(fake.freeaddrinfo_calls, 2);
}

ZTEST(connect_by_name, test_port_from_service)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	zassert_ok(connect_by_name_resolve(&ctx, HOST, "1234", &hints_any, 0, K_NO_WAIT));
	zassert_ok(next_port(buf, RESOLVED_PORT));
}

ZTEST(connect_by_name, test_service_and_flags)
{
	const struct zsock_addrinfo hints = {
		.ai_socktype = NET_SOCK_DGRAM,
		.ai_flags = AI_PDNSERV,
	};
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	zassert_ok(connect_by_name_resolve(&ctx, HOST, "1", &hints, PORT, K_NO_WAIT));
	zassert_str_equal(fake.last_service, "1");
	zassert_equal(fake.last_flags, AI_PDNSERV);
	zassert_ok(next(buf));
}

ZTEST(connect_by_name, test_null_hints)
{
	char buf[NET_IPV6_ADDR_LEN];

	set_state(true, true, true);

	zassert_ok(connect_by_name_resolve(&ctx, HOST, NULL, NULL, PORT, K_NO_WAIT));
	zassert_equal(fake.last_family, NET_AF_UNSPEC);
	zassert_ok(next(buf));
}

ZTEST(connect_by_name, test_invalid)
{
	const struct zsock_addrinfo bad_family = { .ai_family = 99 };
	const struct net_sockaddr *sa;
	net_socklen_t len;

	zassert_equal(connect_by_name_resolve(NULL, HOST, NULL, NULL, PORT, K_NO_WAIT), -EINVAL);
	zassert_equal(connect_by_name_resolve(&ctx, NULL, NULL, NULL, PORT, K_NO_WAIT), -EINVAL);
	zassert_equal(connect_by_name_resolve(&ctx, HOST, NULL, &bad_family, PORT, K_NO_WAIT),
		      -EINVAL);
	zassert_equal(connect_by_name_next(NULL, &sa, &len), -EINVAL);
	zassert_equal(connect_by_name_next(&ctx, NULL, &len), -EINVAL);
	zassert_equal(connect_by_name_next(&ctx, &sa, NULL), -EINVAL);
	connect_by_name_free(NULL);
}

/* A loss alone does not wake the wait */
ZTEST(connect_by_name, test_wait_change_loss)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	set_state(true, false, true);
	zassert_equal(connect_by_name_wait_change(&ctx, K_MSEC(50)), -EAGAIN);
}

/* A family that went down and came back counts as a gain */
ZTEST(connect_by_name, test_wait_change_regain)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	set_state(false, true, true);
	set_state(true, true, true);
	zassert_ok(connect_by_name_wait_change(&ctx, K_NO_WAIT));
}

ZTEST(connect_by_name, test_wait_change_new_family)
{
	int64_t start;

	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(connect_by_name_wait_change(&ctx, K_NO_WAIT), -EAGAIN);

	start = k_uptime_get();
	notify_later(NET_EVENT_L4_IPV6_CONNECTED, K_MSEC(100));
	zassert_ok(connect_by_name_wait_change(&ctx, K_SECONDS(5)));
	zassert_true(k_uptime_get() - start >= 100);
	zassert_true(k_uptime_get() - start < 2000);
}

/* Gains before resolving do not count */
ZTEST(connect_by_name, test_wait_change_since_resolve)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, true);
	set_state(true, true, true);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(connect_by_name_wait_change(&ctx, K_NO_WAIT), -EAGAIN);
}

/* Only state relevant to the context counts */
ZTEST(connect_by_name, test_wait_change_irrelevant)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_CONNECT_BY_NAME_READINESS);
	set_state(true, false, false);

	/* IPv6 is not wanted and DNS is not needed for a numeric host */
	zassert_ok(resolve("192.0.2.7", NET_AF_INET, K_NO_WAIT));
	set_state(true, true, true);
	zassert_equal(connect_by_name_wait_change(&ctx, K_NO_WAIT), -EAGAIN);

	/* DNS is needed for a name */
	set_state(true, false, false);
	zassert_equal(resolve(HOST, NET_AF_INET, K_NO_WAIT), -ENETDOWN);
	set_state(true, false, true);
	zassert_ok(connect_by_name_wait_change(&ctx, K_NO_WAIT));
}

/* Without readiness tracking, waits for the full timeout */
ZTEST(connect_by_name, test_wait_change_no_tracking)
{
	int64_t start = k_uptime_get();

	Z_TEST_SKIP_IFDEF(CONFIG_CONNECT_BY_NAME_READINESS);

	zassert_ok(resolve(HOST, NET_AF_UNSPEC, K_NO_WAIT));
	zassert_equal(connect_by_name_wait_change(&ctx, K_MSEC(100)), -EAGAIN);
	zassert_true(k_uptime_get() - start >= 100);
}

ZTEST_SUITE(connect_by_name, NULL, suite_setup, before, after, NULL);
