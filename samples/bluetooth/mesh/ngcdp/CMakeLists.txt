# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ble_mesh.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

# Add custom bindings path
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dts/bindings)

target_sources(app PRIVATE src/main.c)
