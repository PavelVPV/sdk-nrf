/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Header file for internal nrf_cloud DNS utils. */

#ifndef NRF_CLOUD_NET_UTIL_H_
#define NRF_CLOUD_NET_UTIL_H_

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <net/connect_by_name.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

/** @brief Callback called for every address tried by nrf_cloud_connect_host.
 * Should return either a valid socket, or a negative error code.
 */
typedef int (*nrf_cloud_connect_host_cb)(const struct net_sockaddr *addr, net_socklen_t addrlen);

/**
 * @brief Initialize a connect_by_name context for an nRF Cloud host.
 *
 * If a static IPv4 address is configured, it replaces the host name and DNS is not used.
 * Otherwise, IPv6 is used if enabled for nRF Cloud, and any family if not.
 */
#if defined(CONFIG_NRF_CLOUD_STATIC_IPV4)
#define NRF_CLOUD_CONNECT_BY_NAME_INIT(_host, _port, _socktype)				\
	CONNECT_BY_NAME_INIT(CONFIG_NRF_CLOUD_STATIC_IPV4_ADDR, _port, _socktype, NET_AF_INET)
#else
#define NRF_CLOUD_CONNECT_BY_NAME_INIT(_host, _port, _socktype)				\
	CONNECT_BY_NAME_INIT(_host, _port, _socktype,					\
			     IS_ENABLED(CONFIG_NRF_CLOUD_IPV6) ? NET_AF_INET6 : NET_AF_UNSPEC)
#endif

/**
 * @brief Attempt to connect to an nRF Cloud host.
 *
 * Calls @p connect_cb for each address of the host, see @ref connect_by_name_next. Does not
 * wait for the network.
 *
 * @param ctx Context, see @ref NRF_CLOUD_CONNECT_BY_NAME_INIT.
 * @param connect_cb Callback that connects a socket to the address.
 * @return int Either a negative error code on failure, or a nonnegative socket ID on success.
 */
int nrf_cloud_connect_host(const struct connect_by_name *ctx,
			   nrf_cloud_connect_host_cb connect_cb);

/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* NRF_CLOUD_NET_UTIL_H_ */
