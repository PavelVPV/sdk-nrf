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
 * @brief Resolve a host name for the address families that are up and iterate over the result.
 *
 * The library wraps getaddrinfo(). It does not create sockets, the application connects to
 * each returned address:
 *
 * @code
 * struct connect_by_name host = {0};
 * const struct zsock_addrinfo hints = { .ai_socktype = NET_SOCK_STREAM };
 *
 * err = connect_by_name_resolve(&host, "example.com", NULL, &hints, 443, K_FOREVER);
 * while (err == 0 && connect_by_name_next(&host, &addr, &addrlen) == 0) {
 *	fd = server_connect(addr, addrlen);
 *	if (fd >= 0) {
 *		break;
 *	}
 * }
 * connect_by_name_free(&host);
 * @endcode
 *
 * Connectivity is tracked through connection manager events, see
 * @kconfig{CONFIG_CONNECT_BY_NAME_READINESS}. On nRF91 Series devices without a modem network
 * interface, the modem is queried instead, see @kconfig{CONFIG_CONNECT_BY_NAME_MODEM_READINESS}.
 * Otherwise, the network is assumed ready.
 *
 * A context must be zero-initialized before first use and used from a single thread. It holds
 * the getaddrinfo() result until @ref connect_by_name_free is called.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Host address iterator. Zero-initialize before first use. Fields are internal. */
struct connect_by_name {
	/** @cond INTERNAL_HIDDEN */
	struct zsock_addrinfo *res;
	struct zsock_addrinfo *next;
	/* Sum of the gain counters of mask at resolve */
	uint32_t gains;
	uint16_t port;
	/* State bits the context depends on */
	uint8_t mask;
	/** @endcond */
};

/**
 * @brief Resolve a host for the address families that are up.
 *
 * Frees the previous result, if any. Waits up to @p wait for a wanted address family, and for
 * DNS if @p host is a name, to be up. If only one family is up, only that family is queried.
 * The call can also block in getaddrinfo(), up to the DNS timeout. It cannot be cancelled.
 *
 * @param ctx Context.
 * @param host Host name or numeric address. Not used after the call returns.
 * @param service Service passed to getaddrinfo(), or NULL.
 * @param hints Hints passed to getaddrinfo(), or NULL. @c ai_family selects the wanted family,
 *		@c NET_AF_UNSPEC for any.
 * @param port Port set on the returned addresses, in host byte order. 0 to keep the port
 *	       resolved from @p service.
 * @param wait Maximum time to wait for the network. K_NO_WAIT to not wait.
 *
 * @retval 0 Success. Call @ref connect_by_name_free when done.
 * @retval -ENETDOWN Network not ready within @p wait.
 * @retval -EAGAIN Temporary DNS failure, for example no response from the server.
 * @retval -EHOSTUNREACH The host could not be resolved to a usable address.
 * @retval -ENOMEM Out of memory.
 * @retval -EINVAL Invalid argument.
 */
int connect_by_name_resolve(struct connect_by_name *ctx, const char *host, const char *service,
			    const struct zsock_addrinfo *hints, uint16_t port, k_timeout_t wait);

/**
 * @brief Get the next resolved address.
 *
 * Addresses are returned in getaddrinfo() order. Addresses of families that are not wanted or
 * are down are skipped. The address stays valid until @ref connect_by_name_free or the next
 * @ref connect_by_name_resolve.
 *
 * @param ctx Context.
 * @param[out] addr Next address, including the port.
 * @param[out] addrlen Length of @p addr.
 *
 * @retval 0 Success.
 * @retval -ENOENT No more addresses. Resolve again to start over.
 * @retval -EINVAL Invalid argument.
 */
int connect_by_name_next(struct connect_by_name *ctx, const struct net_sockaddr **addr,
			 net_socklen_t *addrlen);

/**
 * @brief Free the resolved addresses.
 *
 * Can be called on a zero-initialized or already freed context.
 *
 * @param ctx Context.
 */
void connect_by_name_free(struct connect_by_name *ctx);

/**
 * @brief Wait for connectivity to be gained since the last @ref connect_by_name_resolve.
 *
 * Returns when an address family or DNS relevant to the context comes up, including one that
 * went down and came back. A loss alone does not wake the call. Returns immediately if this
 * already happened. With @kconfig{CONFIG_CONNECT_BY_NAME_MODEM_READINESS}, the modem is polled.
 * Without any readiness tracking, waits for the full timeout.
 *
 * @param ctx Context.
 * @param timeout Maximum time to wait, typically the retry delay.
 *
 * @retval 0 Connectivity was gained.
 * @retval -EAGAIN Timeout.
 */
int connect_by_name_wait_change(const struct connect_by_name *ctx, k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* CONNECT_BY_NAME_H_ */
