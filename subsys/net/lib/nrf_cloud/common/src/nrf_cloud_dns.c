/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Internal net lib utils for nrf_cloud. */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <net/connect_by_name.h>

#include "nrf_cloud_dns.h"

LOG_MODULE_REGISTER(nrf_cloud_dns, CONFIG_NRF_CLOUD_LOG_LEVEL);

int nrf_cloud_connect_host(const char *host, uint16_t port, int socktype,
			   nrf_cloud_connect_host_cb connect_cb)
{
	struct zsock_addrinfo hints = {
		.ai_family = IS_ENABLED(CONFIG_NRF_CLOUD_IPV6) ? NET_AF_INET6 : NET_AF_UNSPEC,
		.ai_socktype = socktype,
	};
	struct connect_by_name ctx = {0};
	const struct net_sockaddr *addr;
	net_socklen_t addrlen;
	int sock = -ECONNREFUSED;
	int err;

#if defined(CONFIG_NRF_CLOUD_STATIC_IPV4)
	/* Do not fall back to DNS if a static IP address is set */
	host = CONFIG_NRF_CLOUD_STATIC_IPV4_ADDR;
	hints.ai_family = NET_AF_INET;
#endif

	err = connect_by_name_resolve(&ctx, host, NULL, &hints, port, K_NO_WAIT);
	if (err) {
		LOG_WRN("Cannot resolve nRF Cloud host: %s, error: %d", host, err);
		return err;
	}

	while (connect_by_name_next(&ctx, &addr, &addrlen) == 0) {
		sock = connect_cb(addr, addrlen);
		if (sock >= 0) {
			break;
		}

		LOG_WRN("Failed to connect to nRF Cloud host %s, error: %d", host, sock);
	}

	connect_by_name_free(&ctx);

	if (sock < 0) {
		LOG_WRN("Cannot connect to nRF Cloud host: %s, error: %d", host, sock);
		return sock;
	}

	LOG_DBG("Connected to nRF Cloud host: %s. Socket ID: %d", host, sock);

	return sock;
}
