.. _lib_mqtt_helper:

MQTT helper
###########

.. contents::
   :local:
   :depth: 2

The MQTT helper library is a wrapper around the Zephyr :ref:`zephyr:mqtt_socket_interface` library for Message Queuing Telemetry Transport (MQTT).
It handles the parts of an MQTT connection that most applications implement in the same way.
The library resolves the broker address, opens a TCP or TLS connection, and polls the socket in its own thread.
It also sends keep-alive pings and reports MQTT events to the application through callbacks.
The library uses MQTT version 3.1.1 and supports one MQTT client at a time.

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

Usage
*****

To initialize the library, set the callbacks in the ``cb`` field of the :c:struct:`mqtt_helper_cfg` structure and pass the structure to the :c:func:`mqtt_helper_init` function.
Then, call the :c:func:`mqtt_helper_connect` function to connect to the MQTT broker.
The function returns after the library has sent the CONNECT packet.
The library reports the result later through the ``on_connack`` callback.

The library calls all callbacks from its own thread.
While a callback runs, the library does not read the socket or send keep-alive pings.
For this reason, keep the callbacks short and move longer work to another thread.

The library does not reconnect by itself.
When the connection is lost, the library closes the socket and calls the ``on_disconnect`` callback.
The application decides when to call the :c:func:`mqtt_helper_connect` function again.

.. _lib_mqtt_helper_receiving:

Receiving messages
==================

To receive messages, subscribe to topics after the ``on_connack`` callback reports a successful connection.
Pass a list of topics in the :c:struct:`mqtt_subscription_list` structure to the :c:func:`mqtt_helper_subscribe` function.
The list must have a non-zero message ID.
The library reports the result through the ``on_suback`` callback with the same message ID.
If the broker resumed a previous session, it keeps the subscriptions of that session, so the application does not need to subscribe again.

When a message arrives on a subscribed topic, the library reads the payload into its payload buffer and calls the ``on_publish`` callback.
The topic and the payload are valid only while the callback runs, because the library uses the same buffers for the next message.
If you need the data later, copy it before the callback returns.

For a quality of service (QoS) 1 message, the library sends the publish acknowledgment (PUBACK) packet to the broker before it calls the ``on_publish`` callback.
The broker therefore considers the message delivered as soon as the library has read it, even if the application then fails to process it.

If the payload is larger than the :kconfig:option:`CONFIG_MQTT_HELPER_PAYLOAD_BUFFER_LEN` buffer, the library drops the message and does not send a PUBACK.
It reports the :c:enumerator:`MQTT_HELPER_ERROR_MSG_SIZE` error through the ``on_error`` callback.

.. _lib_mqtt_helper_publishing:

Publishing messages
===================

The :c:func:`mqtt_helper_publish` function passes the publish parameters unchanged to the Zephyr MQTT library.
Each message needs a non-zero message ID, which the application can get from the :c:func:`mqtt_helper_msg_id_get` function.

The library does not keep a copy of a message after it sends it.
When the broker acknowledges a QoS 1 message, the library calls the ``on_puback`` callback with the message ID.
This means that the application is responsible for at-least-once delivery.
Keep each QoS 1 message until the ``on_puback`` callback reports the ID of that message.

After a reconnect, the ``on_connack`` callback tells the application if the broker resumed the previous session.
In this case, the MQTT specification requires the client to resend each unacknowledged QoS 1 message with its original message ID.
The library does not do this, so the application is responsible for it.
Call the :c:func:`mqtt_helper_publish` function for each unacknowledged message.
Use the original message ID, and set the ``dup_flag`` field of the :c:struct:`mqtt_publish_param` structure to ``1``.
If the broker started a new session, it does not know the old message IDs, so publish the messages as new messages.
The broker resumes a session only if the :kconfig:option:`CONFIG_MQTT_CLEAN_SESSION` Kconfig option is disabled, which is the default.

API documentation
*****************

| Header file: :ncs-file:`/include/net/mqtt_helper.h`
| Source file: :ncs-file:`/subsys/net/lib/mqtt_helper/mqtt_helper.c`

.. doxygengroup:: mqtt_helper
