/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <psa/crypto.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_csr.h>
#include <net/device_key.h>

LOG_MODULE_REGISTER(device_key, CONFIG_DEVICE_KEY_LOG_LEVEL);

/* The security tag is the PSA key ID. Returns PSA_KEY_ID_NULL if it is not a valid key ID. */
static psa_key_id_t key_id_get(sec_tag_t sec_tag)
{
	if (sec_tag < 0 || (psa_key_id_t)sec_tag < PSA_KEY_ID_USER_MIN ||
	    (psa_key_id_t)sec_tag > PSA_KEY_ID_USER_MAX) {
		return PSA_KEY_ID_NULL;
	}

	return (psa_key_id_t)sec_tag;
}

static int crypto_init(void)
{
	psa_status_t status = psa_crypto_init();

	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_crypto_init failed, status: %d", status);
		return -EIO;
	}

	return 0;
}

bool device_key_exists(sec_tag_t sec_tag)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = key_id_get(sec_tag);

	if (key_id == PSA_KEY_ID_NULL || crypto_init() != 0) {
		return false;
	}

	if (psa_get_key_attributes(key_id, &attr) != PSA_SUCCESS) {
		return false;
	}

	psa_reset_key_attributes(&attr);

	return true;
}

static int key_register(sec_tag_t sec_tag, psa_key_id_t key_id)
{
	int err;

	/* The volatile TLS credentials backend stores the pointer to the buffer, not a copy, so
	 * the local variable would not be valid after this function returns. The protected storage
	 * backend copies the data, which is why DEVICE_KEY depends on it. It also keeps the
	 * registration after a reboot.
	 */
	err = tls_credential_add(sec_tag, TLS_CREDENTIAL_PRIVATE_KEY_PSA, &key_id,
				 sizeof(key_id));
	if (err == -EEXIST) {
		/* Already registered. */
		return 0;
	} else if (err) {
		LOG_ERR("Failed to register the key for sec tag %d, error: %d", sec_tag, err);
	}

	return err;
}

int device_key_generate(sec_tag_t sec_tag)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = key_id_get(sec_tag);
	psa_status_t status;
	int err;

	if (key_id == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}

	err = crypto_init();
	if (err) {
		return err;
	}

	if (device_key_exists(sec_tag)) {
		LOG_WRN("Key for sec tag %d already exists, delete it before generating a new one",
			sec_tag);
		return -EALREADY;
	}

	/* No PSA_KEY_USAGE_EXPORT, so the private key cannot be read back from PSA. The verify
	 * usages only allow the use of the public key.
	 */
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_SIGN_MESSAGE |
				       PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_PERSISTENT);
	psa_set_key_id(&attr, key_id);

	status = psa_generate_key(&attr, &key_id);
	psa_reset_key_attributes(&attr);

	if (status == PSA_ERROR_ALREADY_EXISTS) {
		return -EALREADY;
	} else if (status != PSA_SUCCESS) {
		LOG_ERR("psa_generate_key failed, status: %d", status);
		return -EIO;
	}

	LOG_INF("Generated device key for sec tag %d", sec_tag);

	err = key_register(sec_tag, key_id);
	if (err) {
		/* Do not leave a key behind that cannot be used. */
		(void)device_key_delete(sec_tag);
		return err;
	}

	return 0;
}

int device_key_delete(sec_tag_t sec_tag)
{
	psa_key_id_t key_id = key_id_get(sec_tag);
	psa_status_t status;
	int err;

	if (key_id == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}

	err = crypto_init();
	if (err) {
		return err;
	}

	(void)tls_credential_delete(sec_tag, TLS_CREDENTIAL_PRIVATE_KEY_PSA);

	status = psa_destroy_key(key_id);
	if (status != PSA_SUCCESS && status != PSA_ERROR_INVALID_HANDLE) {
		LOG_ERR("psa_destroy_key failed, status: %d", status);
		return -EIO;
	}

	LOG_INF("Deleted device key for sec tag %d", sec_tag);

	return 0;
}

static mbedtls_md_type_t md_type_get(psa_algorithm_t alg)
{
	switch (PSA_ALG_GET_HASH(alg)) {
	case PSA_ALG_SHA_384:
		return MBEDTLS_MD_SHA384;
	case PSA_ALG_SHA_512:
		return MBEDTLS_MD_SHA512;
	default:
		return MBEDTLS_MD_SHA256;
	}
}

int device_key_csr_generate(sec_tag_t sec_tag, const char *subject, uint8_t *out,
			    size_t out_size, size_t *out_len)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = key_id_get(sec_tag);
	mbedtls_pk_context pk;
	mbedtls_x509write_csr csr;
	int ret;

	if (subject == NULL || out == NULL || out_len == NULL || key_id == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}

	ret = crypto_init();
	if (ret) {
		return ret;
	}

	if (psa_get_key_attributes(key_id, &attr) != PSA_SUCCESS) {
		return -ENOENT;
	}

	mbedtls_pk_init(&pk);
	mbedtls_x509write_csr_init(&csr);

	/* Wrap the PSA key as an opaque PK. PSA makes the signature of the CSR, so the private
	 * key is never exported.
	 */
	ret = mbedtls_pk_wrap_psa(&pk, mbedtls_svc_key_id_make(0, key_id));
	if (ret != 0) {
		LOG_ERR("mbedtls_pk_wrap_psa failed: -0x%04x", (unsigned int)-ret);
		ret = -EIO;
		goto cleanup;
	}

	/* The hash of the signature follows the algorithm of the key. */
	mbedtls_x509write_csr_set_md_alg(&csr, md_type_get(psa_get_key_algorithm(&attr)));

	ret = mbedtls_x509write_csr_set_subject_name(&csr, subject);
	if (ret != 0) {
		LOG_ERR("Invalid CSR subject name: -0x%04x", (unsigned int)-ret);
		ret = -EINVAL;
		goto cleanup;
	}

	mbedtls_x509write_csr_set_key(&csr, &pk);

	/* mbedtls_x509write_csr_der() writes to the end of the buffer and returns the length, so
	 * move the result to the front.
	 */
	ret = mbedtls_x509write_csr_der(&csr, out, out_size);
	if (ret < 0) {
		LOG_ERR("mbedtls_x509write_csr_der failed: -0x%04x", (unsigned int)-ret);
		ret = -EIO;
		goto cleanup;
	}

	*out_len = (size_t)ret;
	memmove(out, out + out_size - *out_len, *out_len);
	ret = 0;

cleanup:
	mbedtls_x509write_csr_free(&csr);
	mbedtls_pk_free(&pk);
	psa_reset_key_attributes(&attr);

	return ret;
}

int device_key_public_get(sec_tag_t sec_tag, uint8_t *out, size_t out_size, size_t *out_len)
{
	psa_key_id_t key_id = key_id_get(sec_tag);
	psa_status_t status;

	if (!IS_ENABLED(CONFIG_DEVICE_KEY_PUBLIC_EXPORT)) {
		return -ENOTSUP;
	}

	if (out == NULL || out_len == NULL || key_id == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}

	if (crypto_init() != 0) {
		return -EIO;
	}

	/* The public key is not secret, and PSA exports it from the key pair regardless of the
	 * usage of the key.
	 */
	status = psa_export_public_key(key_id, out, out_size, out_len);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_export_public_key failed, status: %d", status);
		return -EIO;
	}

	return 0;
}
