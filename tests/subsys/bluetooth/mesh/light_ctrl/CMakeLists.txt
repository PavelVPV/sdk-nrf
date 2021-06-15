#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.13.1)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(bt_mesh_light_ctrl_test)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

target_sources(app
    PRIVATE
    ${ZEPHYR_BASE}/../nrf/subsys/bluetooth/mesh/light_ctrl_srv.c
    ${ZEPHYR_BASE}/../nrf/subsys/bluetooth/mesh/light_ctrl_cli.c
    ${ZEPHYR_BASE}/subsys/net/buf.c
    )

target_include_directories(app
    PRIVATE
    ${ZEPHYR_BASE}/../nrf/subsys/bluetooth/mesh
    )

target_compile_options(app
    PRIVATE
    -DCONFIG_BT_MESH_MODEL_KEY_COUNT=5
    -DCONFIG_BT_MESH_MODEL_GROUP_COUNT=5
    -DCONFIG_BT_MESH_LIGHT_CTRL_CLI=1
    -DCONFIG_BT_MESH_LIGHT_CTRL_SRV=1
    )

zephyr_ld_options(
    ${LINKERFLAGPREFIX},--allow-multiple-definition
    )
