#!/bin/bash
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Set up a test AP on mac80211_hwsim for the nRF71 UMAC simulator.
# Usage: sudo hwsim_setup.sh up|down
# radio0 = APs in netns nrf71sim: nrf71-sim (open), nrf71-sim-wpa2 (WPA2-PSK) and
# nrf71-sim-wpa3 (WPA3-SAE), bridged in
# br0 with hostapd + dnsmasq on 192.168.71.1. radio1 = STA used by
# zephyr.exe. The netns keeps the AP away from the host firewall and IP stack.
# Run servers for the sample with: sudo ip netns exec nrf71sim <cmd>

set -e
DIR=$(dirname "$(readlink -f "$0")")
STATE=/tmp/nrf71-sim
STA_MAC=${STA_MAC:-F6:CE:36:71:00:01}
AP_IP=192.168.71.1
# Upstream DNS. Zephyr uses dnsmasq on $AP_IP, which forwards to this server.
DNS=${DNS:-8.8.8.8}
NS=nrf71sim
NSX="ip netns exec $NS"

hwsim_ifs() {
	for i in /sys/class/net/*; do
		drv=$(readlink -f "$i/device/driver" 2>/dev/null)
		[ "$(basename "$drv")" = mac80211_hwsim ] && basename "$i"
	done
}

down() {
	[ -f $STATE/hostapd.pid ] && kill "$(cat $STATE/hostapd.pid)" 2>/dev/null || true
	[ -f $STATE/dnsmasq.pid ] && kill "$(cat $STATE/dnsmasq.pid)" 2>/dev/null || true
	sleep 0.5
	modprobe -r mac80211_hwsim 2>/dev/null || true
	ip netns del $NS 2>/dev/null || true
	rm -rf $STATE
}

up() {
	down
	mkdir -p $STATE
	modprobe mac80211_hwsim radios=2
	sleep 1
	set -- $(hwsim_ifs)
	AP_IF=$1
	STA_IF=$2
	for i in $AP_IF $STA_IF; do
		nmcli device set "$i" managed no 2>/dev/null || true
	done
	ip link set "$STA_IF" down
	ip link set "$STA_IF" address "$STA_MAC"
	sysctl -qw net.ipv6.conf."$STA_IF".disable_ipv6=1
	ip link set "$STA_IF" up
	ip netns add $NS
	iw phy "$(cat /sys/class/net/"$AP_IF"/phy80211/name)" set netns name $NS
	$NSX ip link set lo up
	$NSX ip link add br0 type bridge
	$NSX ip addr add $AP_IP/24 dev br0
	$NSX ip link set br0 up
	sed "s/@AP_IF@/$AP_IF/g" "$DIR/hostapd.conf" > $STATE/hostapd.conf
	$NSX hostapd -B -P $STATE/hostapd.pid -f $STATE/hostapd.log $STATE/hostapd.conf
	$NSX dnsmasq --interface=br0 --bind-interfaces --leasefile-ro \
		--no-resolv --server=$DNS --conf-file="$DIR/dnsmasq.conf" \
		--dhcp-range=192.168.71.10,192.168.71.50,1h \
		--dhcp-option=3,$AP_IP --dhcp-option=6,$AP_IP \
		--pid-file=$STATE/dnsmasq.pid --log-facility=$STATE/dnsmasq.log --log-dhcp
	echo "$STA_IF" > $STATE/sta_if
	chmod -R a+rX $STATE
	echo "AP $AP_IF ($AP_IP, netns $NS), STA $STA_IF ($STA_MAC)"
}

case "$1" in
up) up ;;
down) down ;;
*) echo "usage: $0 up|down"; exit 1 ;;
esac
