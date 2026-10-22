/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/net/tls_credentials.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/base64.h>

#include <psa/crypto.h>
#include <mbedtls/x509_csr.h>
#include <net/device_key.h>

#define TEST_SEC_TAG   42
#define TEST_SEC_TAG_2 43

static void reset_state(void)
{
	(void)device_key_delete(TEST_SEC_TAG);
	(void)device_key_delete(TEST_SEC_TAG_2);
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_state();
}

/* Create a persistent key behind the module's back. The security tag is the PSA key ID. */
static void foreign_key_create(psa_key_id_t id, psa_key_usage_t usage, size_t bits,
			       psa_algorithm_t alg)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t out;

	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, bits);
	psa_set_key_usage_flags(&attr, usage);
	psa_set_key_algorithm(&attr, alg);
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_PERSISTENT);
	psa_set_key_id(&attr, id);
	zassert_equal(psa_generate_key(&attr, &out), PSA_SUCCESS, "PSA key generation failed");
}

ZTEST_SUITE(device_key_api, NULL, NULL, before, NULL, NULL);

ZTEST(device_key_api, test_generate_and_exists)
{
	zassert_false(device_key_exists(TEST_SEC_TAG), "key must not exist yet");
	zassert_ok(device_key_generate(TEST_SEC_TAG), "generate failed");
	zassert_true(device_key_exists(TEST_SEC_TAG), "key must exist");
	zassert_equal(device_key_generate(TEST_SEC_TAG), -EALREADY,
		      "second generate must report -EALREADY");
}

ZTEST(device_key_api, test_generate_defaults)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	zassert_ok(device_key_generate(TEST_SEC_TAG));
	zassert_equal(psa_get_key_attributes(TEST_SEC_TAG, &attr), PSA_SUCCESS);
	zassert_equal(psa_get_key_type(&attr), PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	zassert_equal(psa_get_key_bits(&attr), 256);
	zassert_equal(psa_get_key_algorithm(&attr), PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	zassert_false(PSA_KEY_LIFETIME_IS_VOLATILE(psa_get_key_lifetime(&attr)),
		      "key must be persistent");
	zassert_false(psa_get_key_usage_flags(&attr) & PSA_KEY_USAGE_EXPORT,
		      "key must not be exportable");
	psa_reset_key_attributes(&attr);
}

ZTEST(device_key_api, test_user_key_id_range)
{
	/* Only the PSA user key ID range is accepted, so keys of other kinds are out of reach. */
	zassert_equal(device_key_generate(0), -EINVAL);
	zassert_equal(device_key_generate(-1), -EINVAL);
	zassert_equal(device_key_generate(PSA_KEY_ID_USER_MAX + 1), -EINVAL);
	zassert_equal(device_key_generate(INT32_MAX), -EINVAL);
	zassert_false(device_key_exists(0));
	zassert_false(device_key_exists(PSA_KEY_ID_USER_MAX + 1));
	zassert_equal(device_key_delete(0), -EINVAL);
	zassert_equal(device_key_delete(PSA_KEY_ID_USER_MAX + 1), -EINVAL);
}

ZTEST(device_key_api, test_delete)
{
	zassert_ok(device_key_generate(TEST_SEC_TAG));
	zassert_ok(device_key_delete(TEST_SEC_TAG));
	zassert_false(device_key_exists(TEST_SEC_TAG));

	/* Deleting a key that does not exist is not an error. */
	zassert_ok(device_key_delete(TEST_SEC_TAG));
}

ZTEST(device_key_api, test_pubkey_get)
{
	uint8_t pub[65];
	size_t len = 0;

	zassert_ok(device_key_generate(TEST_SEC_TAG));

	zassert_equal(device_key_public_get(TEST_SEC_TAG, NULL, sizeof(pub), &len), -EINVAL,
		      "NULL output must be rejected");
	zassert_ok(device_key_public_get(TEST_SEC_TAG, pub, sizeof(pub), &len));
	zassert_equal(len, 65, "P-256 SEC1 public key must be 65 bytes");
	zassert_equal(pub[0], 0x04, "uncompressed point must start with 0x04");
}

ZTEST(device_key_api, test_csr_generate)
{
	uint8_t der[512];
	size_t len = 0;
	mbedtls_x509_csr csr;
	char subj[64];

	zassert_ok(device_key_generate(TEST_SEC_TAG));

	zassert_equal(device_key_csr_generate(TEST_SEC_TAG, NULL, der, sizeof(der), &len), -EINVAL,
		      "NULL subject must be rejected");
	zassert_equal(device_key_csr_generate(TEST_SEC_TAG, "not a subject", der, sizeof(der),
					      &len),
		      -EINVAL, "invalid subject must be rejected");
	zassert_equal(device_key_csr_generate(TEST_SEC_TAG_2, "CN=test", der, sizeof(der), &len),
		      -ENOENT, "missing key must be reported");

	zassert_ok(device_key_csr_generate(TEST_SEC_TAG, "CN=test,O=Nordic", der, sizeof(der),
					   &len));
	zassert_true(len > 0, "CSR length must be positive");
	zassert_equal(der[0], 0x30, "DER CSR must begin with a SEQUENCE tag");

	/* Re-parsing validates the structure, the subject and the signature made through PSA. */
	mbedtls_x509_csr_init(&csr);
	zassert_equal(mbedtls_x509_csr_parse_der(&csr, der, len), 0, "CSR does not parse");
	zassert_true(mbedtls_x509_dn_gets(subj, sizeof(subj), &csr.subject) > 0);
	zassert_not_null(strstr(subj, "CN=test"), "unexpected CSR subject: %s", subj);
	zassert_not_null(strstr(subj, "O=Nordic"), "unexpected CSR subject: %s", subj);
	zassert_equal(csr.MBEDTLS_PRIVATE(sig_md), MBEDTLS_MD_SHA256,
		      "P-256 key must sign with SHA-256");
	mbedtls_x509_csr_free(&csr);
}

ZTEST(device_key_api, test_csr_follows_key_algorithm)
{
	uint8_t der[512];
	size_t len = 0;
	mbedtls_x509_csr csr;

	/* A key of another curve that was not made by this library. */
	foreign_key_create(TEST_SEC_TAG, PSA_KEY_USAGE_SIGN_HASH, 384,
			   PSA_ALG_ECDSA(PSA_ALG_SHA_384));

	zassert_ok(device_key_csr_generate(TEST_SEC_TAG, "CN=test", der, sizeof(der), &len));

	mbedtls_x509_csr_init(&csr);
	zassert_equal(mbedtls_x509_csr_parse_der(&csr, der, len), 0, "CSR does not parse");
	zassert_equal(csr.MBEDTLS_PRIVATE(sig_md), MBEDTLS_MD_SHA384,
		      "P-384 key must sign with SHA-384");
	mbedtls_x509_csr_free(&csr);
}

ZTEST_SUITE(device_key_tls, NULL, NULL, before, NULL, NULL);

ZTEST(device_key_tls, test_generate_registers_key)
{
	uint32_t key_id = 0;
	size_t len = sizeof(key_id);

	zassert_ok(device_key_generate(TEST_SEC_TAG));
	zassert_ok(tls_credential_get(TEST_SEC_TAG, TLS_CREDENTIAL_PRIVATE_KEY_PSA, &key_id, &len),
		   "key not registered");
	zassert_equal(key_id, TEST_SEC_TAG, "registered key id mismatch");
}

ZTEST(device_key_tls, test_two_sec_tags)
{
	uint32_t key_id = 0;
	size_t len = sizeof(key_id);

	zassert_ok(device_key_generate(TEST_SEC_TAG));
	zassert_ok(device_key_generate(TEST_SEC_TAG_2));

	zassert_ok(tls_credential_get(TEST_SEC_TAG, TLS_CREDENTIAL_PRIVATE_KEY_PSA, &key_id, &len));
	zassert_equal(key_id, TEST_SEC_TAG);
	len = sizeof(key_id);
	zassert_ok(tls_credential_get(TEST_SEC_TAG_2, TLS_CREDENTIAL_PRIVATE_KEY_PSA, &key_id,
				      &len));
	zassert_equal(key_id, TEST_SEC_TAG_2);

	/* Deleting one key keeps the other. */
	zassert_ok(device_key_delete(TEST_SEC_TAG));
	zassert_false(device_key_exists(TEST_SEC_TAG));
	zassert_true(device_key_exists(TEST_SEC_TAG_2));
}

ZTEST(device_key_tls, test_delete_removes_registration)
{
	uint32_t key_id = 0;
	size_t len = sizeof(key_id);

	zassert_ok(device_key_generate(TEST_SEC_TAG));
	zassert_ok(device_key_delete(TEST_SEC_TAG));
	zassert_not_equal(tls_credential_get(TEST_SEC_TAG, TLS_CREDENTIAL_PRIVATE_KEY_PSA, &key_id,
					     &len), 0, "registration must be removed");
}

ZTEST(device_key_tls, test_rollback_on_registration_failure)
{
	static const uint8_t dummy[] = {0xAA};
	const sec_tag_t fill_tags[] = {9001, 9002, 9003, 9004};

	BUILD_ASSERT(ARRAY_SIZE(fill_tags) >= CONFIG_TLS_MAX_CREDENTIALS_NUMBER,
		     "must be able to fill the whole TLS credentials store");

	/* Fill the TLS credentials store so the registration fails after the key is generated. */
	for (int i = 0; i < CONFIG_TLS_MAX_CREDENTIALS_NUMBER; i++) {
		zassert_ok(tls_credential_add(fill_tags[i], TLS_CREDENTIAL_CA_CERTIFICATE, dummy,
					      sizeof(dummy)));
	}

	zassert_not_equal(device_key_generate(TEST_SEC_TAG), 0,
			  "generate must fail when the registration cannot complete");
	zassert_false(device_key_exists(TEST_SEC_TAG),
		      "failed registration must roll back the generated key");

	for (int i = 0; i < CONFIG_TLS_MAX_CREDENTIALS_NUMBER; i++) {
		(void)tls_credential_delete(fill_tags[i], TLS_CREDENTIAL_CA_CERTIFICATE);
	}
}

/* ---------------------------------------------------------------------------
 * Shell tests. Host tooling (nrfcredstore)
 * scrapes the command output, so the exact markers below are a contract.
 * ---------------------------------------------------------------------------
 */

static const struct shell *dummy;

static void *shell_setup(void)
{
	dummy = shell_backend_dummy_get_ptr();
	WAIT_FOR(shell_ready(dummy), 20000, k_msleep(1));
	zassert_true(shell_ready(dummy), "timed out waiting for dummy shell backend");
	return NULL;
}

static void shell_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_state();
	shell_backend_dummy_clear_output(dummy);
}

/* Execute a command and return the captured output. */
static const char *run(const char *cmd, int *ret)
{
	size_t size;
	int r;

	shell_backend_dummy_clear_output(dummy);
	r = shell_execute_cmd(dummy, cmd);
	if (ret != NULL) {
		*ret = r;
	}
	return shell_backend_dummy_get_output(dummy, &size);
}

/* Copy the whitespace-delimited token that follows @p prefix in @p out into
 * @p tok. Returns the token length, or 0 if @p prefix is not found.
 */
static size_t token_after(const char *out, const char *prefix, char *tok, size_t tok_size)
{
	const char *p = strstr(out, prefix);
	size_t i = 0;

	if (p == NULL) {
		return 0;
	}
	p += strlen(prefix);

	while (p[i] != '\0' && p[i] != '\r' && p[i] != '\n' && p[i] != ' ' && i < tok_size - 1) {
		tok[i] = p[i];
		i++;
	}
	tok[i] = '\0';

	return i;
}

/* True if every character is in the standard Base64 alphabet. */
static bool is_base64(const char *s)
{
	for (; *s != '\0'; s++) {
		char c = *s;

		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		      (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) {
			return false;
		}
	}

	return true;
}

ZTEST_SUITE(device_key_shell, NULL, shell_setup, shell_before, NULL, NULL);

ZTEST(device_key_shell, test_shell_keygen_output)
{
	int ret;
	const char *out = run("device-key generate 42", &ret);

	zassert_ok(ret, "keygen command returned %d", ret);
	zassert_not_null(strstr(out, "Generated device key in sec tag 42"),
			 "missing success line, got: %s", out);
	zassert_true(device_key_exists(TEST_SEC_TAG),
		     "key not created by shell command");
}

ZTEST(device_key_shell, test_shell_keygen_already_exists)
{
	int ret;
	const char *out;

	(void)run("device-key generate 42", NULL);
	out = run("device-key generate 42", &ret);

	zassert_not_equal(ret, 0, "second keygen should fail");
	zassert_not_null(strstr(out, "Key already exists"), "missing warning, got: %s", out);
	zassert_not_null(strstr(out, "delete"), "should point user to 'delete', got: %s", out);
}

ZTEST(device_key_shell, test_shell_csr_output)
{
	int ret;
	const char *out;

	char tok[1024];
	uint8_t der[512];
	size_t tok_len;
	size_t der_len = 0;
	mbedtls_x509_csr csr;

	(void)run("device-key generate 42", NULL);
	out = run("device-key csr 42 CN=test,O=Nordic", &ret);

	zassert_ok(ret, "csr command returned %d", ret);
	/* Host tooling captures the Base64 DER from the "CSR: " prefix and uses
	 * the completion marker to know the output is done.
	 */
	zassert_not_null(strstr(out, "CSR: "), "missing 'CSR: ' prefix, got: %s", out);
	zassert_not_null(strstr(out, "CSR generation complete"),
			 "missing completion marker, got: %s", out);

	/* The printed CSR must be valid, complete Base64. Decoding it and parsing
	 * the result as a CSR proves the line is real Base64 and was not
	 * truncated: a cut-off line would decode to a malformed DER that fails to
	 * parse.
	 */
	tok_len = token_after(out, "CSR: ", tok, sizeof(tok));
	zassert_true(tok_len > 0, "no CSR token in output: %s", out);
	zassert_true(is_base64(tok), "CSR is not Base64: %s", tok);
	zassert_ok(base64_decode(der, sizeof(der), &der_len, tok, tok_len),
		   "CSR Base64 does not decode (truncated?)");
	zassert_true(der_len > 0, "decoded CSR is empty");

	mbedtls_x509_csr_init(&csr);
	zassert_equal(mbedtls_x509_csr_parse_der(&csr, der, der_len), 0,
		      "decoded CSR does not parse (truncated?)");
	mbedtls_x509_csr_free(&csr);
}

ZTEST(device_key_shell, test_shell_csr_requires_subject)
{
	int ret;

	(void)run("device-key generate 42", NULL);
	(void)run("device-key csr 42", &ret);

	zassert_not_equal(ret, 0, "csr without a subject should fail");
}

ZTEST(device_key_shell, test_shell_csr_invalid_subject)
{
	int ret;
	const char *out;

	(void)run("device-key generate 42", NULL);
	out = run("device-key csr 42 notasubject", &ret);

	zassert_not_equal(ret, 0, "csr with an invalid subject should fail");
	zassert_not_null(strstr(out, "CSR generation failed"), "missing error, got: %s", out);
}

ZTEST(device_key_shell, test_shell_pubkey_output)
{
	int ret;
	const char *out;

	char tok[128];
	uint8_t dec[80];
	size_t tok_len;
	size_t dec_len = 0;

	(void)run("device-key generate 42", NULL);
	out = run("device-key pubkey 42", &ret);

	zassert_ok(ret, "pubkey command returned %d", ret);
	zassert_not_null(strstr(out, "PUBKEY: "), "missing 'PUBKEY: ' prefix, got: %s", out);
	zassert_not_null(strstr(out, "Public key export complete"),
			 "missing completion marker, got: %s", out);

	/* The printed key must be valid, complete Base64. A 65-byte SEC1 point
	 * encodes to exactly 88 Base64 characters; decoding back to 65 bytes
	 * starting with 0x04 proves it is neither malformed nor truncated.
	 */
	tok_len = token_after(out, "PUBKEY: ", tok, sizeof(tok));
	zassert_equal(tok_len, 88, "public key Base64 length %zu != 88 (truncated?)", tok_len);
	zassert_true(is_base64(tok), "public key is not Base64: %s", tok);
	zassert_ok(base64_decode(dec, sizeof(dec), &dec_len, tok, tok_len),
		   "public key Base64 does not decode");
	zassert_equal(dec_len, 65, "decoded public key is %zu bytes, expected 65", dec_len);
	zassert_equal(dec[0], 0x04, "decoded public key is not an uncompressed point");
}

ZTEST(device_key_shell, test_shell_delete_output)
{
	int ret;
	const char *out;

	(void)run("device-key generate 42", NULL);
	out = run("device-key delete 42", &ret);

	zassert_ok(ret, "delete command returned %d", ret);
	zassert_not_null(strstr(out, "Deleted device key in sec tag 42"),
			 "missing delete confirmation, got: %s", out);
	zassert_false(device_key_exists(TEST_SEC_TAG),
		      "key still present after shell delete");
}

ZTEST(device_key_shell, test_shell_invalid_sec_tag)
{
	int ret;
	const char *out = run("device-key generate notanumber", &ret);

	zassert_not_equal(ret, 0, "invalid sec tag should fail");
	zassert_not_null(strstr(out, "not a valid sectag"), "missing error, got: %s", out);
}
