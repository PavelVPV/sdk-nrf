/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CONNECT_BY_NAME_H_
#define CONNECT_BY_NAME_H_

/**
 * @file connect_by_name.h
 * @defgroup connect_by_name Host address iterator
 * @{
 * @brief Resolve a host name and iterate over the addresses that are usable now.
 *
 * The library does not create sockets. The application connects to each returned address:
 *
 * @code
 * static struct connect_by_name host =
 *	CONNECT_BY_NAME_INIT("example.com", 5684, NET_SOCK_DGRAM, NET_AF_UNSPEC);
 *
 * while (connect_by_name_next(&host, &addr, &len, K_FOREVER) == 0) {
 *	fd = zsock_socket(addr->sa_family, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
 *	if (fd >= 0 && zsock_connect(fd, addr, len) == 0) {
 *		break;
 *	}
 *	if (fd >= 0) {
 *		zsock_close(fd);
 *		fd = -1;
 *	}
 * }
 * @endcode
 *
 * The host is resolved on the first call, after the last address was returned, and when the
 * address families that are up changed since the last resolve. Addresses of families that
 * are down are skipped.
 *
 * Connectivity is tracked through connection manager events, see
 * @kconfig{CONFIG_CONNECT_BY_NAME_READINESS}. On nRF91 Series devices without a modem network
 * interface, the modem is queried instead, see @kconfig{CONFIG_CONNECT_BY_NAME_MODEM_READINESS}.
 * Otherwise, the network is assumed ready.
 *
 * A context must be used from a single thread. It holds no resources, so it needs no
 * deinitialization. A context that is not static must stay in scope while it is used, and the
 * address it returns is valid only until the next call.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */
/** @brief Runtime data of a host address iterator. */
struct connect_by_name_data {
	struct net_sockaddr_storage addrs[CONFIG_CONNECT_BY_NAME_MAX_ADDRS];
	uint8_t addr_count;	/* 0: not resolved */
	uint8_t addr_next;
	uint8_t resolved;	/* Families that were up at resolve */
};
/** @endcond */

/**
 * @brief Host address iterator.
 *
 * The configuration is constant. Use @ref CONNECT_BY_NAME_INIT to define it together with its
 * runtime data.
 */
struct connect_by_name {
	/** Host name or numeric address. */
	const char *const host;
	/** Port, in host byte order. */
	const uint16_t port;
	/** Socket type passed to getaddrinfo(), for example NET_SOCK_DGRAM. */
	const int socktype;
	/** @c NET_AF_INET, @c NET_AF_INET6, or @c NET_AF_UNSPEC for any. */
	const int family;
	/** Optional service for getaddrinfo(). The port is always set from @c port. */
	const char *const service;
	/** Flags for getaddrinfo() hints. */
	const int ai_flags;
	/** @cond INTERNAL_HIDDEN */
	struct connect_by_name_data *const data;
	/** @endcond */
};

/**
 * @brief Initialize a host address iterator.
 *
 * The runtime data is a compound literal, so it has static storage duration when the iterator
 * is defined at file scope, and the storage duration of the enclosing block otherwise. A
 * @c static iterator inside a function is not supported, define it at file scope instead.
 *
 * @param _host Host name or numeric address.
 * @param _port Port, in host byte order.
 * @param _socktype Socket type passed to getaddrinfo(), for example NET_SOCK_DGRAM.
 * @param _family @c NET_AF_INET, @c NET_AF_INET6, or @c NET_AF_UNSPEC for any.
 * @param ... Optional designated initializers of other @ref connect_by_name fields, for example
 *	      @c .service and @c .ai_flags.
 */
#define CONNECT_BY_NAME_INIT(_host, _port, _socktype, _family, ...)                             \
	{                                                                                       \
		.host = (_host),                                                                \
		.port = (_port),                                                                \
		.socktype = (_socktype),                                                        \
		.family = (_family),                                                            \
		.data = &(struct connect_by_name_data){},                                        \
		__VA_ARGS__                                                                     \
	}

/**
 * @brief Get the next address to try.
 *
 * Waits up to @p wait for a wanted address family, and for DNS if the host is a name, to be
 * up. Then resolves the host if needed and returns the next address. The call can also block
 * in getaddrinfo(), up to the DNS timeout. It cannot be cancelled.
 *
 * The address stays valid until the next call with the same context.
 *
 * @param ctx Context.
 * @param[out] addr Next address, including the port.
 * @param[out] addrlen Length of @p addr.
 * @param wait Maximum time to wait for the network. K_NO_WAIT to not wait.
 *
 * @retval 0 Success.
 * @retval -ENETDOWN Network not ready within @p wait.
 * @retval -EHOSTUNREACH The host could not be resolved to a usable address.
 * @retval -ENOENT No more addresses. The next call resolves again.
 * @retval -EINVAL Invalid context.
 */
int connect_by_name_next(const struct connect_by_name *ctx, const struct net_sockaddr **addr,
			 net_socklen_t *addrlen, k_timeout_t wait);

/**
 * @brief Forget the resolved addresses. The next call resolves again.
 *
 * @param ctx Context.
 */
void connect_by_name_reset(const struct connect_by_name *ctx);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* CONNECT_BY_NAME_H_ */
