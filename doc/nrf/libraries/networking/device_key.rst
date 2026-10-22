.. _lib_device_key:

Device key
##########

.. contents::
   :local:
   :depth: 2

The device key library generates a private key on the device and a Certificate Signing Request (CSR) that the key signs.
The private key never leaves the device.
You send the CSR to a certificate authority to obtain the device certificate.

.. note::
   The library is :ref:`experimental <software_maturity>`.

Overview
********

The library uses the :ref:`PSA Crypto API <ug_psa_certified_api_overview_crypto>` to generate the key.
The key is persistent and cannot be exported.
The key is an ECC P-256 key that signs with ECDSA and SHA-256.
The library takes the hash of the CSR signature from the algorithm of the key.

The security tag of the upper layer is the PSA key ID of the key.
The library accepts only key IDs in the PSA user key ID range, which is ``PSA_KEY_ID_USER_MIN`` to ``PSA_KEY_ID_USER_MAX``.
PSA does not track which user owns a key, so the upper layer must choose security tags that no other user of the PSA Crypto API uses.
See :file:`zephyr/include/zephyr/psa/key_ids.h` for the key ID ranges of the known users.

The library registers a key as a TLS credential of the type ``TLS_CREDENTIAL_PRIVATE_KEY_PSA``.
The :kconfig:option:`CONFIG_DEVICE_KEY` Kconfig option requires the TLS credentials protected storage backend (:kconfig:option:`CONFIG_TLS_CREDENTIALS_BACKEND_PROTECTED_STORAGE`).
The volatile backend stores the pointer to the credential, not a copy, so it would need storage for each key.
The protected storage backend copies the credential and keeps it after a reboot, so the library does not store anything.
The key is registered when it is generated, and the registration is removed when the key is deleted.

Configuration
*************

To use the library, enable the :kconfig:option:`CONFIG_DEVICE_KEY` Kconfig option.
Configure the following options as needed:

* :kconfig:option:`CONFIG_DEVICE_KEY_PUBLIC_EXPORT` - Enables the export of the public key.
* :kconfig:option:`CONFIG_DEVICE_KEY_SHELL` - Enables the shell commands.

Shell commands
**************

When the :kconfig:option:`CONFIG_DEVICE_KEY_SHELL` Kconfig option is enabled, the following commands are available.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Command
     - Description
   * - ``device-key generate <sec_tag>``
     - Generates a key and registers it for the security tag.
   * - ``device-key csr <sec_tag> <subject>``
     - Prints the Base64 encoded DER CSR, for example for the subject ``"CN=my-device,O=Nordic"``.
   * - ``device-key pubkey <sec_tag>``
     - Prints the Base64 encoded public key.
       Requires the :kconfig:option:`CONFIG_DEVICE_KEY_PUBLIC_EXPORT` Kconfig option.
   * - ``device-key delete <sec_tag>``
     - Deletes the key and its registration.

API documentation
*****************

| Header file: :file:`include/net/device_key.h`
| Source files: :file:`subsys/net/lib/device_key/`

.. doxygengroup:: device_key
