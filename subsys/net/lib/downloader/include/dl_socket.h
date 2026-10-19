/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef DL_SOCKET_H
#define DL_SOCKET_H

#include <net/downloader.h>
#include <sys/types.h>

/* Connects to dl->hostname. Tries the last working address first, then all resolved ones. */
int dl_socket_connect(struct downloader *dl, int *fd, int proto, int type, uint16_t port);
/* Forgets the last working address, call when the host, port or protocol changes. */
void dl_socket_host_reset(struct downloader *dl);
/* Reports that the current connection is not usable: the next connect tries other addresses. */
void dl_socket_failed(struct downloader *dl);
const struct net_sockaddr *dl_socket_peer(struct downloader *dl);
int dl_socket_close(int *fd);
int dl_socket_send(int fd, void *buf, size_t len);
ssize_t dl_socket_recv(int fd, void *buf, size_t len);
int dl_socket_recv_timeout_set(int fd, uint32_t timeout_ms);
int dl_socket_send_timeout_set(int fd, uint32_t timeout_ms);

#endif /* DL_SOCKET_H */
