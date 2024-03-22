# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mesh_lte_gateway)

set(dfu_common_dir ${ZEPHYR_NRF_MODULE_DIR}/samples/bluetooth/mesh/dfu/common/src)

include_directories(
	include/
	${dfu_common_dir}
	${ZEPHYR_NRF_MODULE_DIR}/samples/bluetooth/mesh/common
)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources}
	${dfu_common_dir}/dfu_target.c
)
