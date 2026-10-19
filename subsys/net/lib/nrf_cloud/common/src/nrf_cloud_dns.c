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

int nrf_cloud_connect_host(const struct connect_by_name *ctx,
			   nrf_cloud_connect_host_cb connect_cb)
{
	const struct net_sockaddr *addr;
	net_socklen_t addrlen;
	int sock = -ECONNREFUSED;
	int err;

	connect_by_name_reset(ctx);

	while ((err = connect_by_name_next(ctx, &addr, &addrlen, K_NO_WAIT)) == 0) {
		sock = connect_cb(addr, addrlen);
		if (sock >= 0) {
			LOG_DBG("Connected to nRF Cloud host: %s. Socket ID: %d", ctx->host, sock);
			return sock;
		}

		LOG_WRN("Failed to connect to nRF Cloud host %s, error: %d", ctx->host, sock);
	}

	/* -ENOENT: all addresses tried, return the last connection error */
	err = (err == -ENOENT) ? sock : err;

	LOG_WRN("Cannot connect to nRF Cloud host: %s, error: %d", ctx->host, err);

	return err;
}
