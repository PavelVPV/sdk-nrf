/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <errno.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_credentials_keygen.h>
#include <net/device_key.h>

#include "nrf_cloud_credentials_keygen_internal.h"

int nrf_cloud_credentials_key_generate(uint32_t sec_tag)
{
	return device_key_generate((sec_tag_t)sec_tag);
}

int nrf_cloud_credentials_key_delete(uint32_t sec_tag)
{
	return device_key_delete((sec_tag_t)sec_tag);
}

int nrf_cloud_credentials_csr_generate(uint32_t sec_tag, const char *subject,
				       uint8_t *out, size_t out_size, size_t *out_len)
{
	return device_key_csr_generate((sec_tag_t)sec_tag, subject, out, out_size,
				       out_len);
}

int nrf_cloud_credentials_pubkey_get(uint32_t sec_tag, uint8_t *out, size_t out_size,
				     size_t *out_len)
{
	if (!IS_ENABLED(CONFIG_NRF_CLOUD_CREDENTIALS_KEYGEN_VERIFY)) {
		return -ENOTSUP;
	}

	return device_key_public_get((sec_tag_t)sec_tag, out, out_size, out_len);
}

psa_key_id_t nrf_cloud_credentials_keygen_key_id(uint32_t sec_tag)
{
	return (psa_key_id_t)sec_tag;
}

bool nrf_cloud_credentials_keygen_key_exists(uint32_t sec_tag)
{
	return device_key_exists((sec_tag_t)sec_tag);
}
