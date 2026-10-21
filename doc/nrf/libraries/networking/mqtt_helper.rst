.. _lib_mqtt_helper:

MQTT helper
###########

.. contents::
   :local:
   :depth: 2

This library simplifies Zephyr MQTT API and socket handling.

Configuration
*************

To use the MQTT helper library, enable the :kconfig:option:`CONFIG_MQTT_HELPER` Kconfig option.

Additionally, configure the following options as per the needs of your application:

* :kconfig:option:`CONFIG_MQTT_HELPER_NATIVE_TLS`
* :kconfig:option:`CONFIG_MQTT_HELPER_PORT`
* :kconfig:option:`CONFIG_MQTT_HELPER_SEC_TAG`
* :kconfig:option:`CONFIG_MQTT_HELPER_SEND_TIMEOUT`
* :kconfig:option:`CONFIG_MQTT_HELPER_SEND_TIMEOUT_SEC`
* :kconfig:option:`CONFIG_MQTT_HELPER_STATIC_IP_ADDRESS`
* :kconfig:option:`CONFIG_MQTT_HELPER_SECONDARY_SEC_TAG`
* :kconfig:option:`CONFIG_MQTT_HELPER_STACK_SIZE`
* :kconfig:option:`CONFIG_MQTT_HELPER_RX_TX_BUFFER_SIZE`
* :kconfig:option:`CONFIG_MQTT_HELPER_PAYLOAD_BUFFER_LEN`
* :kconfig:option:`CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES`
* :kconfig:option:`CONFIG_MQTT_HELPER_CERTIFICATES_FOLDER`

On-device private key
=====================

When the :kconfig:option:`CONFIG_TLS_CREDENTIALS_KEYGEN` Kconfig option is enabled, the library registers a device private key that was generated on the device before it connects.
The key is generated with the :ref:`TLS credentials shell <zephyr:tls_credentials_shell>` ``cred key`` commands or with the TLS credentials API, and it never leaves the device.
The registration is not persistent, so the library calls the ``tls_credentials_key_restore()`` function for each security tag used for the connection every time it connects.
A security tag that has no generated key is skipped.
Only one security tag can hold a generated key.
See :ref:`aws_iot` and :ref:`azure_iot_hub` for an example of how to generate the key and obtain the device certificate.

API documentation
*****************

| Header file: :file:`include/net/mqtt_helper.h`
| Source file: :file:`subsys/net/lib/mqtt_helper/mqtt_helper.c`

.. doxygengroup:: mqtt_helper
