#!/usr/bin/env python3
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""UDP server for the udp sample on the nRF71 sim.

Run in the AP namespace: sudo ip netns exec nrf71sim ./udp_server.py [port]
"""

import socket
import sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 2469
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", port))
print(f"Listening on UDP port {port}", flush=True)
while True:
    data, addr = s.recvfrom(2048)
    print(f"{addr[0]}:{addr[1]} {len(data)} B: {data.hex()}", flush=True)
