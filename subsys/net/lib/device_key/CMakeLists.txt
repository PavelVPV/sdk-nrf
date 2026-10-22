#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()
zephyr_library_sources(device_key.c)
zephyr_library_sources_ifdef(CONFIG_DEVICE_KEY_SHELL device_key_shell.c)
