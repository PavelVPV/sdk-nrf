.. _wifi_monitor_sample:

Wi-Fi: Monitor
##############

.. contents::
   :local:
   :depth: 2

The Monitor sample demonstrates how to set monitor mode, analyze incoming Wi-Fi® packets, and print packet statistics.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

Overview
********

The sample demonstrates how to configure the nRF70 Series device in Monitor mode.
It analyzes the incoming Wi-Fi packets on a raw socket and prints the packet statistics at a fixed interval.

To set the wait duration for printing Wi-Fi packet statistics in seconds, use the :kconfig:option:`CONFIG_STATS_PRINT_TIMEOUT` Kconfig option.

Configuration
*************

|config|

Configuration options
=====================

The following sample-specific Kconfig options are used in this sample (located in :file:`samples/wifi/monitor/Kconfig`):

.. options-from-kconfig::

Building and running
********************

.. |sample path| replace:: :file:`samples/wifi/monitor`

.. include:: /includes/build_and_run_ns.txt

To build for the nRF7002 DK, use the ``nrf7002dk/nrf5340/cpuapp`` board target.
The following is an example of the CLI command:

.. code-block:: console

   west build -b nrf7002dk/nrf5340/cpuapp

Change the board target as given below for the nRF7002 EK.

.. code-block:: console

   nrf5340dk/nrf5340/cpuapp -- -DSHIELD=nrf7002ek

.. include:: /includes/wifi_refer_sample_yaml_file.txt

Testing
=======

|test_sample|

#. |connect_kit|
#. |connect_terminal|

   The sample shows the following output:

   .. code-block:: console

      [00:00:00.422,027] <dbg> net_if: net_if_init: (0x20002d68):
      [00:00:00.422,088] <dbg> net_if: init_iface: (0x20002d68): On iface 0x20001448
      [00:00:00.422,210] <dbg> net_if: update_operational_state: (0x20002d68): iface 0x20001448, oper state DOWN admin DOWN carrier ON dormant ON
      [00:00:00.422,271] <dbg> net_if: net_if_ipv6_calc_reachable_time: (0x20002d68): min_reachable:15000 max_reachable:45000
      [00:00:00.422,485] <dbg> net_if: net_if_post_init: (0x20002d68):
      [00:00:00.422,485] <dbg> net_if: net_if_up: (0x20002d68): iface 0x20001448
      [00:00:00.486,083] <dbg> net_if: update_operational_state: (0x20002d68): iface 0x20001448, oper state DORMANT admin UP carrier ON dormant ON
      *** Booting nRF Connect SDK v3.4.99-ncs1-4979-g258a846cfb5d ***
      [00:00:00.486,267] <inf> net_config: Initializing network
      [00:00:00.486,297] <inf> net_config: Waiting interface 1 (0x20001448) to be up...
      [00:00:00.486,419] <inf> monitor: Waiting for packets ...
      [00:00:00.487,609] <inf> monitor: Mode set to Monitor
      [00:00:00.488,220] <dbg> net_if: update_operational_state: (0x20002e20): iface 0x20001448, oper state UP admin UP carrier ON dormant OFF
      [00:00:00.488,220] <dbg> net_if: net_if_start_dad: (0x20002e20): Starting DAD for iface 0x20001448
      [00:00:00.488,311] <dbg> net_if: net_if_ipv6_addr_add: (0x20002e20): [0] interface 0x20001448 address fe80::f6ce:36ff:fe00:16 type AUTO added
      [00:00:00.488,372] <dbg> net_if: net_if_ipv6_maddr_add: (0x20002e20): [0] interface 0x20001448 address ff02::1 added
      [00:00:00.488,647] <dbg> net_if: net_if_ipv6_maddr_add: (0x20002e20): [1] interface 0x20001448 address ff02::1:ff00:16 added
      [00:00:00.488,922] <dbg> net_if: net_if_ipv6_start_dad: (0x20002e20): Interface 0x20001448 ll addr F4:CE:36:00:00:16 tentative IPv6 addr fe80::f6ce:36ff:fe00:16
      [00:00:00.489,074] <dbg> net_if: net_if_start_rs: (0x20002e20): Starting ND/RS for iface 0x20001448
      [00:00:00.489,288] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x200588a0, prio 1) network packet iface 0x20001448/1
      [00:00:00.489,776] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x2005885c, prio 1) network packet iface 0x20001448/1
      [00:00:00.489,837] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x20058818, prio 1) network packet iface 0x20001448/1
      [00:00:00.489,868] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x200587d4, prio 1) network packet iface 0x20001448/1
      [00:00:00.490,509] <inf> monitor: Wi-Fi channel set to 1
      [00:00:00.589,172] <dbg> net_if: dad_timeout: (0x20002e20): DAD succeeded for fe80::f6ce:36ff:fe00:16
      [00:00:00.589,263] <inf> net_config: IPv6 address: fe80::f6ce:36ff:fe00:16
      [00:00:01.489,288] <dbg> net_if: rs_timeout: (0x20002e20): RS no respond iface 0x20001448 count 1
      [00:00:01.489,288] <dbg> net_if: net_if_start_rs: (0x20002e20): Starting ND/RS for iface 0x20001448
      [00:00:01.489,440] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x200587d4, prio 1) network packet iface 0x20001448/1
      [00:00:02.489,501] <dbg> net_if: rs_timeout: (0x20002e20): RS no respond iface 0x20001448 count 2
      [00:00:02.489,532] <dbg> net_if: net_if_start_rs: (0x20002e20): Starting ND/RS for iface 0x20001448
      [00:00:02.489,685] <dbg> net_if: net_if_tx: (0x20002518): Processing (pkt 0x200587d4, prio 1) network packet iface 0x20001448/1
      [00:00:03.489,746] <dbg> net_if: rs_timeout: (0x20002e20): RS no respond iface 0x20001448 count 3
      [00:00:05.492,889] <inf> monitor: Management Frames:
      [00:00:05.492,919] <inf> monitor:       Beacon Count: 451
      [00:00:05.492,919] <inf> monitor:       Probe Request Count: 20
      [00:00:05.492,919] <inf> monitor:       Probe Response Count: 194
      [00:00:05.492,919] <inf> monitor: Control Frames:
      [00:00:05.492,919] <inf> monitor:        Ack Count 34
      [00:00:05.492,950] <inf> monitor:        RTS Count 4
      [00:00:05.492,950] <inf> monitor:        CTS Count 82
      [00:00:05.492,950] <inf> monitor:        Block Ack Count 0
      [00:00:05.492,950] <inf> monitor:        Block Ack Req Count 0
      [00:00:05.492,980] <inf> monitor: Data Frames:
      [00:00:05.492,980] <inf> monitor:       Data Count: 5
      [00:00:05.492,980] <inf> monitor:       QoS Data Count: 0
      [00:00:05.492,980] <inf> monitor:       Null Count: 0
      [00:00:05.493,011] <inf> monitor:       QoS Null Count: 0

Offline net capture
*******************

.. include:: /includes/offline_net_capture.txt

Troubleshooting
****************

USB connector for offline capture
==================================

The DK exposes two separate USB connectors. Only the one wired to the SoC's own USB
peripheral carries the CDC-ECM network device used for offline capture
(:file:`overlay-netusb.conf`) — the J-Link/debug USB connector only provides programming
and the serial console, and never exposes a network interface. If the sample logs
``Network capture of Wi-Fi traffic enabled`` but the host shows no new ``enx...``
interface (or a ``net_if send failure status -115`` log), plug a cable into the
board's dedicated USB connector in addition to the debug one.

Truncated or incomplete captured frames
========================================

:kconfig:option:`CONFIG_MONITOR_MODE_WIFI_PACKET_FILTER_CAPTURE_LEN` defaults to 64
bytes, which truncates every captured frame at that length. This is enough for headers
and short frames, but not for larger management or EAPOL frames (for example, the WPA/WPA2
4-way handshake), where truncation can silently break offline decryption in a packet
analyzer. Raise this option (up to 1552, its maximum) if you need full-frame capture.

Channel selection, regulatory domain, and DFS
==============================================

:kconfig:option:`CONFIG_MONITOR_MODE_CHANNEL` is fixed for the whole capture session; the
sample does not scan or follow the AP. Set it to the channel your AP/STA link actually
uses, and set :kconfig:option:`CONFIG_MONITOR_MODE_REG_DOMAIN_ALPHA2` to your real country
code — the default world domain (``"00"``) can prevent the radio from tuning to some 5GHz
channels.

If the AP operates on a DFS channel (5GHz, roughly channels 52-144), also enable
:kconfig:option:`CONFIG_WIFI_NRF70_SCAN_DISABLE_DFS_CHANNELS` to hold that channel
steady. Note that an AP on a DFS channel can still perform a radar-avoidance Channel
Switch Announcement at any time; the sample does not follow such switches, so the
capture will go silent until the sample is reconfigured for the new channel.

Dependencies
************

This sample uses the following |NCS| library:

* :ref:`nrf_security`
