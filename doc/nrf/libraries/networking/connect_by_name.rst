.. _lib_connect_by_name:

Host address iterator
#####################

.. contents::
   :local:
   :depth: 2

The host address iterator library resolves a host name and iterates over the addresses that are usable now.

Overview
********

The library resolves the host for the address families that are currently up and returns the resolved addresses one by one, in resolver order.
It does not create sockets.
The application creates a socket, sets the socket options (for example TLS credentials), and connects to each returned address.
A numeric address is used as is, without DNS.

The host is resolved again in the following cases:

* The first call.
* The call after the last address was returned.
* The set of address families that are up changed since the host was resolved.

Addresses of address families that are down are skipped.

Getting the next address
========================

The :c:func:`connect_by_name_next` function returns the next address to try.
It waits up to the given time for a wanted address family, and for DNS if the host is a name, to be up.
Use ``K_NO_WAIT`` to not wait.
The call can also block in ``getaddrinfo()``, up to the DNS timeout.

The function returns ``0`` on success, or one of the following errors:

* ``-ENETDOWN`` - No wanted address family is up, or no DNS server is available for a host name, within the wait time.
* ``-EHOSTUNREACH`` - The host could not be resolved to a usable address.
* ``-ENOENT`` - All addresses were returned.
  The next call resolves the host again.
* ``-EINVAL`` - Invalid context or arguments.

The returned address is valid until the next call with the same context.
Copy it if you need it after that.

The following example connects to a host and retries until a connection is made:

.. code-block:: c

   static struct connect_by_name host =
           CONNECT_BY_NAME_INIT("example.com", 80, NET_SOCK_STREAM, NET_AF_UNSPEC);

   const struct net_sockaddr *addr;
   net_socklen_t addrlen;
   int fd = -1;

   while (fd < 0) {
           if (connect_by_name_next(&host, &addr, &addrlen, K_FOREVER) != 0) {
                   /* All addresses were tried, or the host could not be resolved */
                   k_sleep(K_SECONDS(10));
                   continue;
           }

           fd = zsock_socket(addr->sa_family, NET_SOCK_STREAM, NET_IPPROTO_TCP);
           if (fd >= 0 && zsock_connect(fd, addr, addrlen) < 0) {
                   zsock_close(fd);
                   fd = -1;
           }
   }

If the connection turns out to be unusable after connecting, for example because of a request timeout, close the socket and call the :c:func:`connect_by_name_next` function to get another address.
To connect to the same address again, for example to resume a DTLS session, keep a copy of the address in the application.

Contexts
========

A context is a :c:struct:`connect_by_name` structure.
Its configuration is constant, so you define it with the :c:macro:`CONNECT_BY_NAME_INIT` macro, which takes the host, port, socket type, and address family.
You can pass more fields as designated initializers at the end of the macro, for example ``.service`` and ``.ai_flags``.
The macro also defines the runtime data of the context, as a compound literal.
The runtime data has static storage duration when the context is defined at file scope.
Otherwise, it exists as long as the block that defines the context.
A static context inside a function is not supported.

A context holds no resources, so there is no deinitialization.
To use a different host or port, define a new context.
Call the :c:func:`connect_by_name_reset` function to forget the resolved addresses of a context.

Readiness tracking
==================

The library supports the following ways of tracking connectivity:

* Connection manager events (:kconfig:option:`CONFIG_CONNECT_BY_NAME_READINESS`) - The library tracks IPv4, IPv6, and DNS readiness through the :ref:`zephyr:conn_mgr_overview`.
  This is the default when the connection manager is enabled, except on nRF91 Series devices without the modem network interface.
* Modem polling (:kconfig:option:`CONFIG_CONNECT_BY_NAME_MODEM_READINESS`) - On nRF91 Series devices without the modem network interface, the library queries the IP addresses of the default PDN with the ``AT+CGPADDR`` command.
  The :c:func:`connect_by_name_next` function polls the modem while it waits.
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
* :kconfig:option:`CONFIG_CONNECT_BY_NAME_MAX_ADDRS`

Limitations
***********

A context must be used from a single thread.
Contexts are independent of each other.

API documentation
*****************

| Header file: :file:`include/net/connect_by_name.h`
| Source files: :file:`subsys/net/lib/connect_by_name`

.. doxygengroup:: connect_by_name
