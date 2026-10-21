/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <vtf_monitoring/vtf_monitoring.h>

/* VTF monitoring is not built on native_sim. The driver only passes the address to the UMAC. */
volatile union vtf_sample_value vtf_snapshots[VTF_CH_COUNT] __aligned(4);
