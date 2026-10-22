/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/base64.h>
#include <psa/crypto.h>
#include <net/device_key.h>

#define BASE64_ENCODED_SIZE(n) ((((n) + 2) / 3) * 4 + 1)

/* The buffer holds the DER encoded CSR or the public key, and the Base64 buffer holds its text. */
static uint8_t raw_buf[CONFIG_DEVICE_KEY_SHELL_BUF_SIZE];
static char b64_buf[BASE64_ENCODED_SIZE(CONFIG_DEVICE_KEY_SHELL_BUF_SIZE)];

/* Host tools, for example nrfcredstore, read the output of the commands below, so the markers
 * are a contract: "Generated device key", "CSR:" and "CSR generation complete", "PUBKEY:" and
 * "Public key export complete", and "Deleted device key".
 */

static int sec_tag_parse(const struct shell *sh, const char *arg, int *sec_tag)
{
	char *end;
	long value;

	value = strtol(arg, &end, 10);
	if (arg[0] == '\0' || *end != '\0' || value < (long)PSA_KEY_ID_USER_MIN ||
	    value > (long)PSA_KEY_ID_USER_MAX) {
		shell_error(sh, "%s is not a valid sectag.", arg);
		return -EINVAL;
	}

	*sec_tag = (int)value;

	return 0;
}

static int print_base64(const struct shell *sh, const char *label, size_t raw_len)
{
	size_t b64_len;
	int err;

	err = base64_encode(b64_buf, sizeof(b64_buf), &b64_len, raw_buf, raw_len);
	if (err) {
		shell_error(sh, "Base64 encoding failed: %d", err);
		return err;
	}

	b64_buf[b64_len] = '\0';
	shell_print(sh, "%s %s", label, b64_buf);

	return 0;
}

static int cmd_generate(const struct shell *sh, size_t argc, char *argv[])
{
	int sec_tag;
	int err = sec_tag_parse(sh, argv[1], &sec_tag);

	if (err) {
		return err;
	}

	err = device_key_generate(sec_tag);
	if (err == -EALREADY) {
		shell_warn(sh, "Key already exists in sec tag %d. Use 'delete' to remove it first.",
			   sec_tag);
		return err;
	} else if (err) {
		shell_error(sh, "Key generation failed: %d", err);
		return err;
	}

	shell_print(sh, "Generated device key in sec tag %d", sec_tag);

	return 0;
}

static int cmd_csr(const struct shell *sh, size_t argc, char *argv[])
{
	size_t len;
	int sec_tag;
	int err = sec_tag_parse(sh, argv[1], &sec_tag);

	if (err) {
		return err;
	}

	err = device_key_csr_generate(sec_tag, argv[2], raw_buf,
				      sizeof(raw_buf), &len);
	if (err) {
		shell_error(sh, "CSR generation failed: %d", err);
		return err;
	}

	err = print_base64(sh, "CSR:", len);
	if (err) {
		return err;
	}

	shell_print(sh, "CSR generation complete");

	return 0;
}

static int cmd_delete(const struct shell *sh, size_t argc, char *argv[])
{
	int sec_tag;
	int err = sec_tag_parse(sh, argv[1], &sec_tag);

	if (err) {
		return err;
	}

	err = device_key_delete(sec_tag);
	if (err) {
		shell_error(sh, "Key deletion failed: %d", err);
		return err;
	}

	shell_print(sh, "Deleted device key in sec tag %d", sec_tag);

	return 0;
}

#if defined(CONFIG_DEVICE_KEY_PUBLIC_EXPORT)
static int cmd_pubkey(const struct shell *sh, size_t argc, char *argv[])
{
	size_t len;
	int sec_tag;
	int err = sec_tag_parse(sh, argv[1], &sec_tag);

	if (err) {
		return err;
	}

	err = device_key_public_get(sec_tag, raw_buf, sizeof(raw_buf), &len);
	if (err) {
		shell_error(sh, "Public key export failed: %d", err);
		return err;
	}

	err = print_base64(sh, "PUBKEY:", len);
	if (err) {
		return err;
	}

	shell_print(sh, "Public key export complete");

	return 0;
}
#endif /* CONFIG_DEVICE_KEY_PUBLIC_EXPORT */

SHELL_STATIC_SUBCMD_SET_CREATE(device_key_cmds,
	SHELL_CMD_ARG(generate, NULL,
		      "Generate a device private key.\n"
		      "Usage: device-key generate <sectag>",
		      cmd_generate, 2, 0),
	SHELL_CMD_ARG(csr, NULL,
		      "Generate and print a Base64 DER CSR signed by the device key.\n"
		      "Usage: device-key csr <sectag> <subject>, for example \"CN=my-device,O=Nordic\"",
		      cmd_csr, 3, 0),
	SHELL_CMD_ARG(delete, NULL,
		      "Delete the device private key.\n"
		      "Usage: device-key delete <sectag>",
		      cmd_delete, 2, 0),
	SHELL_COND_CMD_ARG(CONFIG_DEVICE_KEY_PUBLIC_EXPORT, pubkey, NULL,
			   "Export and print the device public key (Base64).\n"
			   "Usage: device-key pubkey <sectag>",
			   cmd_pubkey, 2, 0),
	SHELL_SUBCMD_SET_END
);

/* Not SHELL_CMD_REGISTER(): it uses the name in C identifiers, where a hyphen is invalid. */
static const struct shell_static_entry device_key_root =
	SHELL_CMD_ARG(device-key, &device_key_cmds, "Device private key commands", NULL, 0, 0);
static const TYPE_SECTION_ITERABLE(union shell_cmd_entry, device_key_root_entry,
				   shell_root_cmds, device_key_root_entry) = {
	.entry = &device_key_root
};
