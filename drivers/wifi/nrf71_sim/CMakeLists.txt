# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

zephyr_library_named(nrf71_sim)
zephyr_library_include_directories(${ZEPHYR_NRF_MODULE_DIR}/drivers/wifi/nrf71/inc)
zephyr_library_sources(
  ipc_backend.c
  umac_core.c
  umac_conn.c
  umac_data.c
  umac_scan.c
  vtf_stub.c
)

target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nsim_wifi_host.c)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/nrf71_sim)
zephyr_library_include_directories(${gen_dir})
generate_inc_file_for_target(${ZEPHYR_CURRENT_LIBRARY}
  ${CMAKE_CURRENT_SOURCE_DIR}/golden/new_wiphy.bin
  ${gen_dir}/new_wiphy.bin.inc
)
