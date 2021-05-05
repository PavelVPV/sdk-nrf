/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * PDS - Power Down Storage
 */

// TODO: Provide required space?
int pds_init(void);
int pds_erase(void);
int pds_store(uint16_t id, void *data, uint16_t len);
int pds_store_finalize(void);
int pds_load(uint16_t *id, void *data, size_t *len);
