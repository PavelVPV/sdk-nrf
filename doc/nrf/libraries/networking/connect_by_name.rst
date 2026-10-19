.. _lib_connect_by_name:

Host address iterator
#####################

.. contents::
   :local:
   :depth: 2

The host address iterator library resolves a host name for the address families that are up and iterates over the resolved addresses.

Overview
********

The library wraps the ``getaddrinfo()`` function.
It does not create sockets.
The application creates a socket, sets the socket options (for example TLS credentials), and connects to each returned address.

The library provides the following functions:

* :c:func:`connect_by_name_resolve` - Waits for the network, resolves the host, and keeps the result.
* :c:func:`connect_by_name_next` - Returns the next resolved address.
* :c:func:`connect_by_name_free` - Frees the resolved addresses.
* :c:func:`connect_by_name_wait_change` - Waits for connectivity to be gained.

Resolving a host
================

The :c:func:`connect_by_name_resolve` function waits up to the given time for a wanted address family, and for DNS if the host is a name, to be up.
Use ``K_NO_WAIT`` to not wait.
The wanted family is the ``ai_family`` field of the hints, or any family for ``NET_AF_UNSPEC``.
A numeric address is used as is, without DNS.

If only one address family is up, only that family is resolved.
The call can also block in ``getaddrinfo()``, up to the DNS timeout.

The function returns ``0`` on success, or one of the following errors:

* ``-ENETDOWN`` - No wanted address family is up, or no DNS server is available for a host name, within the wait time.
* ``-EAGAIN`` - Temporary DNS failure, for example no response from the DNS server.
* ``-EHOSTUNREACH`` - The host could not be resolved to a usable address.
* ``-ENOMEM`` - Out of memory.
* ``-EINVAL`` - Invalid arguments.

The function frees the previous result of the context, if any.

Iterating over the addresses
============================

The :c:func:`connect_by_name_next` function returns the resolved addresses one by one, in resolver order, with the port set.
Addresses of address families that are not wanted or are down are skipped.
When all addresses were returned, the function returns ``-ENOENT``.
Resolve the host again to start over.

The returned address is valid until the result is freed or the host is resolved again.
Copy it if you need it after that.

Freeing the result
==================

The resolved addresses are allocated by ``getaddrinfo()``.
Call the :c:func:`connect_by_name_free` function when you do not need them anymore.
The function can be called on a zero-initialized context and more than once.

Waiting for connectivity
========================

The :c:func:`connect_by_name_wait_change` function waits for connectivity to be gained since the last call to the :c:func:`connect_by_name_resolve` function.
It returns when an address family or DNS relevant to the context comes up, including one that went down and came back.
A loss alone does not wake the function.
Use it as the retry delay after all addresses failed.

Example
=======

The following example connects to a host and retries until a connection is made:

.. code-block:: c

   const struct zsock_addrinfo hints = { .ai_socktype = NET_SOCK_STREAM };
   struct connect_by_name host = {0};
   const struct net_sockaddr *addr;
   net_socklen_t addrlen;
   int fd = -1;
   int err;

   while (fd < 0) {
           err = connect_by_name_resolve(&host, "example.com", NULL, &hints, 80, K_FOREVER);

           while (err == 0 && fd < 0 && connect_by_name_next(&host, &addr, &addrlen) == 0) {
                   fd = zsock_socket(addr->sa_family, NET_SOCK_STREAM, NET_IPPROTO_TCP);
                   if (fd >= 0 && zsock_connect(fd, addr, addrlen) < 0) {
                           zsock_close(fd);
                           fd = -1;
                   }
           }

           if (fd < 0) {
                   connect_by_name_wait_change(&host, K_SECONDS(10));
           }
   }

   connect_by_name_free(&host);

If the connection turns out to be unusable after connecting, for example because of a request timeout, close the socket and call the :c:func:`connect_by_name_next` function to get another address.
Keep the result until then.
To connect to the same address again, for example to resume a DTLS session, keep a copy of the address in the application.

Readiness tracking
==================

The library supports the following ways of tracking connectivity:

* Connection manager events (:kconfig:option:`CONFIG_CONNECT_BY_NAME_READINESS`) - The library tracks IPv4, IPv6, and DNS readiness through the :ref:`zephyr:conn_mgr_overview`.
  This is the default when the connection manager is enabled, except on nRF91 Series devices without the modem network interface.
* Modem polling (:kconfig:option:`CONFIG_CONNECT_BY_NAME_MODEM_READINESS`) - On nRF91 Series devices without the modem network interface, the library queries the IP addresses of the default PDN with the ``AT+CGPADDR`` command.
  The library polls the modem while it waits.
* None - The network is assumed ready.

The library does not notify the application when a connection loses its address family.
The application can use the network management events for this.

Configuration
*************

To use the library, enable the :kconfig:option:`CONFIG_CONNECT_BY_NAME` Kconfig option.

You can also configure the following options:

* :kconfig:option:`CONFIG_CONNECT_BY_NAME_READINESS`
* :kconfig:option:`CONFIG_CONNECT_BY_NAME_MODEM_READINESS`
* :kconfig:option:`CONFIG_CONNECT_BY_NAME_MODEM_POLL_INTERVAL_MS`

Limitations
***********

A context must be zero-initialized before first use and used from a single thread.
Contexts are independent of each other.

API documentation
*****************

| Header file: :file:`include/net/connect_by_name.h`
| Source files: :file:`subsys/net/lib/connect_by_name`

.. doxygengroup:: connect_by_name
