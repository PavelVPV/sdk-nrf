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

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

/** @brief Callback called for every address tried by nrf_cloud_connect_host.
 * Should return either a valid socket, or a negative error code.
 */
typedef int (*nrf_cloud_connect_host_cb)(const struct net_sockaddr *addr, net_socklen_t addrlen);

/**
 * @brief Attempt to connect to an nRF Cloud host.
 *
 * Resolves the host with connect_by_name and calls @p connect_cb for each address until it
 * succeeds. Does not wait for the network. If a static IPv4 address is configured, it
 * replaces the host name. Otherwise, IPv6 is used if enabled for nRF Cloud, and any family if
 * not.
 *
 * @param host Host name.
 * @param port Port, in host byte order.
 * @param socktype Socket type, for example NET_SOCK_STREAM.
 * @param connect_cb Callback that connects a socket to the address.
 * @return int Either a negative error code on failure, or a nonnegative socket ID on success.
 */
int nrf_cloud_connect_host(const char *host, uint16_t port, int socktype,
			   nrf_cloud_connect_host_cb connect_cb);

/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* NRF_CLOUD_NET_UTIL_H_ */
