.. _ug_bt_mesh_vendor_model:

Creating a new model
####################

.. contents::
   :local:
   :depth: 2

This user guide describes the basics of creating new models for the Bluetooth Mesh in |NCS|.
You may implement your own vendor-specific model that will enable your devices to provide custom behavior not covered by the already defined standard models.

This user guide is based on the concepts and principals defined in :ref:`mesh_concepts` and :ref:`Access API <zephyr:bluetooth_mesh_access>`.
Therefore, before reading this guide, make sure that you have read and understood them.

In this guide we are going to implement a :ref:`bluetooth_mesh_chat_client_model` model, from the :ref:`bluetooth_mesh_chat` sample.

Overview of the Bluetooth Mesh model development
************************************************

Before we are going to implement our new model, we need to go through the basics of the Bluetooth Mesh model development.

Defining a model identifier
===========================

Models in the Bluetooth Mesh network define a basic functionality of the node.
Once you decided on the functionality that your model will define, you must specify its identifier to be able to recognize it among the other models in the mesh network.

As defined by the `Bluetooth Mesh Profile Specification`_, vendor models identifier is composed of two unique 16 bits values, which specify a company identifier (Company ID) and vendor-assigned model identifier (Model ID) tied to this Company ID:

.. code-block:: c

    #define YOUR_COMPANY_ID 0x1234
    #define YOUR_MODEL_ID   0x5678

Defining opcodes for the messages
=================================

A communication between the nodes within the mesh network is done by means of message exchange.
Therefore, if you want to implement your own behavior of the node, you need to define your own messages that will be associated with this beahvior.
To do that, you need to define vendor-specific operation codes for new messages.

To define vendor-specific opcodes, you can use the :c:macro:`BT_MESH_MODEL_OP_3` macro.
This macro encodes an opcode into the special format defined by the `Bluetooth Mesh Profile Specification`_.
Each vendor-specific message must be tied with a Company ID, which is passed as a second parameter to the macro:

.. code-block:: c

    BT_MESH_MODEL_OP_3(0x01, YOUR_COMPANY_ID)

The two most significant bits of the first octet in a vendor-specific opcode are always set to 1.
Therefore, you can specify up to 64 different vendor-specific opcodes.
The :c:macro:`BT_MESH_MODEL_OP_3` macro takes care of this for you.

You can wrap your opcode in a macro to make it convenient to use in the future:

.. code-block:: c

    #define MESSAGE_SET_OPCODE    BT_MESH_MODEL_OP_3(0x01, YOUR_COMPANY_ID)
    #define MESSAGE_ACK_OPCODE    BT_MESH_MODEL_OP_3(0x02, YOUR_COMPANY_ID)
    #define MESSAGE_STATUS_OPCODE BT_MESH_MODEL_OP_3(0x03, YOUR_COMPANY_ID)

Adding the model to a node composition data
===========================================

Once your model has its own Model ID, it can be added to a node composition data.
This will register the model on the node and enable the model configuration through the :ref:`zephyr:bluetooth_mesh_models_cfg_cli` so that it can communicate with the other models in the network.
Note that the node composition data is passed to :c:func:`bt_mesh_init` at the mesh stack initialization,
therefore it can not be changed at run-time.

To add your model to the node composition data, use :c:macro:`BT_MESH_MODEL_VND_CB` macro, which requiers at least Company ID and Model ID to be provided.
A simplest initialization of a model in the node composition data may look like this:

.. code-block:: c

    BT_MESH_MODEL_VND_CB(YOUR_COMPANY_ID,
                         YOUR_MODEL_ID,
                         BT_MESH_MODEL_NO_OPS,
                         NULL,
                         NULL,
                         NULL)

The third argument here, :c:macro:`BT_MESH_MODEL_NO_OPS` macro, specifies an empty Opcode list, which means that the model won't receive any messages.
The other arguments are set to NULL as optional.
The next sections describe how and when to use these arguments.

Note that vendor models are passed in the second parameter of :c:macro:`BT_MESH_ELEM`, when defining the node composition data:

.. code-block:: c

    static struct bt_mesh_elem elements[] = {
        BT_MESH_ELEM(
            1,
            BT_MESH_MODEL_LIST(BT_MESH_MODEL_CFG_SRV(&cfg_srv),
                               BT_MESH_MODEL_HEALTH_SRV(&health_srv,
                                                        &health_pub)),
            BT_MESH_MODEL_LIST(BT_MESH_MODEL_VND_CB(YOUR_COMPANY_ID,
                                                    YOUR_MODEL_ID,
                                                    BT_MESH_MODEL_NO_OPS,
                                                    NULL,
                                                    NULL,
                                                    NULL))
        ),
    };

Receiving messages
==================

If you want your model to receive messages, you need to create an opcode list.
It defines a list of messages that your model will receive.
To create the opcode list, initialize an array of :c:struct:`bt_mesh_model_op` type with the following required parameters:

1. Message opcode, :c:member:`bt_mesh_model_op.opcode`, to register a message to be received by the model.
#. Minimal message length, :c:member:`bt_mesh_model_op.min_len`, that prevents the model from receiving messages shorter than the specified value.
#. Message handler, :c:member:`bt_mesh_model_op.func`, which is used to process the received message.

Note that the last element in the opcode list is always :c:macro:`BT_MESH_MODEL_OP_END`:

.. code-block:: c

    static void handle_message_set(struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf)
    {
        // Message handler code
    }

    static void handle_message_ack(struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf)
    {
        // Message handler code
    }

    static void handle_message_status(struct bt_mesh_model *model,
                                      struct bt_mesh_msg_ctx *ctx,
                                      struct net_buf_simple *buf)
    {
        // Message handler code
    }

    const struct bt_mesh_model_op _opcode_list[] = {
        { MESSAGE_SET_OPCODE,    MESSAGE_SET_LEN,    handle_message_set },
        { MESSAGE_ACK_OPCODE,    MESSAGE_ACK_LEN,    handle_message_ack },
        { MESSAGE_STATUS_OPCODE, MESSAGE_STATUS_LEN, handle_message_status },
        BT_MESH_MODEL_OP_END,
    };

To associate the opcode list with your model, use :c:macro:`BT_MESH_MODEL_VND_CB`.
It will initialize the :c:member:`bt_mesh_model.op` field of the model context.

Sending messages
================

Before sending a message, you need to prepare a buffer that will contain the message data together with the opcode.
This can be done using the :c:macro:`BT_MESH_MODEL_BUF_DEFINE` macro.
It creates and initializes an instance of :c:struct:`net_buf_simple`, therefore, use :ref:`net_buf_interface` API to fill up the buffer:

.. code-block:: c

    BT_MESH_MODEL_BUF_DEFINE(buf, MESSAGE_SET_OPCODE, MESSAGE_SET_LEN);

To set the opcode of the message, call :c:func:`bt_mesh_model_msg_init`:

.. code-block:: c

    bt_mesh_model_msg_init(&buf, MESSAGE_SET_OPCODE);

As described in the :ref:`Access API <zephyr:bluetooth_mesh_access>`, the model can send a message in two ways:

1. By using a custom :c:struct:`bt_mesh_msg_ctx`.
#. By using a model publication context.

If you want your model to control a destination address or some other parameters of a message,
you can initialize :c:struct:`bt_mesh_msg_ctx` with a custom parameters and pass it together with a message buffer to :c:func:`bt_mesh_model_send`:

.. code-block:: c

    static int send_message(struct bt_mesh_model *model, uint16_t addr)
    {
        struct bt_mesh_msg_ctx ctx = { 0 };

        BT_MESH_MODEL_BUF_DEFINE(buf, MESSAGE_SET_OPCODE, MESSAGE_SET_LEN);
        bt_mesh_model_msg_init(&buf, MESSAGE_SET_OPCODE);

        // Fill the message buffer here

        ctx.addr = addr;
        ctx.send_ttl = model->pub->ttl;
        ctx.send_rel = model->pub->send_rel;
        ctx.app_idx = 0;

        return bt_mesh_model_send(model, &ctx, &buf, NULL, NULL);
    }

The :c:func:`bt_mesh_model_send` function is also used if you need to send a reply on a received message.
To do that, use the message context passed to a handler of a message needs to be replied to, when calling :c:func:`bt_mesh_model_send`:

.. code-block:: c

    static void handle_message_set(struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf)
    {
        BT_MESH_MODEL_BUF_DEFINE(reply, MESSAGE_ACK_OPCODE, MESSAGE_ACK_LEN);
        bt_mesh_model_msg_init(&reply, MESSAGE_ACK_OPCODE);

        // Fill the reply buffer here

        (void) bt_mesh_model_send(model, ctx, &reply, NULL, NULL);
    }

The model publication context defines behavior of messages to be published by the model and is configured by a Configuration Client.
If you want your model to send messages using the model publication context, create :c:struct:`bt_mesh_model_pub` instance and pass it to :c:macro:`BT_MESH_MODEL_VND_CB` to initialize :c:member:`bt_mesh_model.pub`.
You must initialize :c:member:`bt_mesh_model_pub.msg` publication buffer.
This can be done in two ways.
By using the :c:macro:`NET_BUF_SIMPLE` macro:

.. code-block:: c

    static struct bt_mesh_model_pub pub_ctx = {
        .msg = NET_BUF_SIMPLE(BT_MESH_MODEL_BUF_LEN(MESSAGE_SET_OPCODE,
                                                    MESSAGE_SET_MAXLEN)),
    }

Or e.g. in the :c:member:`bt_mesh_model_cb.init` callback, using :c:func:`net_buf_simple_init_with_data`:

.. code-block:: c

    static struct bt_mesh_model_pub pub_ctx;
    static struct net_buf_simple pub_msg;
    static uint8_t buf[BT_MESH_MODEL_BUF_LEN(MESSAGE_SET_OPCODE,
                                             MESSAGE_SET_MAXLEN)];

    static int model_init(struct bt_mesh_model *model)
    {
        model->pub = &pub_ctx;
        net_buf_simple_init_with_data(&pub_msg, buf, sizeof(buf));
        pub_ctx.msg = &pub_msg;

        return 0;
    }

Note that the publication buffer size must be big enough to fit the longest message to be published.
How to initialize :c:member:`bt_mesh_model_cb.init` is described later in this guide.

When your model supports the model publication, you can use the Configuration Client to configure the model to send messages at certain periods regardless of the current model state.
This can be useful, if you need your model to publish data periodically, e.g. if it changes over time.
To do that, initialize the :c:member:`bt_mesh_model_pub.update` callback.

If the periodic publication is configured by the Configuration Client, the access layer calls the :c:member:`bt_mesh_model_pub.update` callback in the beginning of each period.
It resets the buffer provided in :c:member:`bt_mesh_model_pub.msg`.
Therefore, you only need to fill the data into the buffer:

.. code-block:: c

    static int update_handler(struct bt_mesh_model *model)
    {
        bt_mesh_model_msg_init(model->pub->msg, MESSAGE_STATUS_OPCODE);

        // Fill the model publication buffer here

        return 0;
    }

User data
=========

You can associate your data with the :c:struct:`bt_mesh_model` structure through the :c:member:`bt_mesh_model.user_data` field.
This can be useful if you need to restore your context associated with the model whenever any of callbacks defined by the Access API are called.

To do that, pass the pointer to your data to :c:macro:`BT_MESH_MODEL_VND_CB`.

Defining model callbacks
========================

The Access API provides a set of callbacks that are called when certain events occur.
These callbacks are defined in :c:struct:`bt_mesh_model_cb`:

:c:member:`bt_mesh_model_cb.settings_set`
   This handler is called when the model data is restored from persistent storage.
   If you need to store a data in persistent storage, use :c:func:`bt_mesh_model_data_store`.
   Note that to use persitent storage, it needs to be enabled with :option:`CONFIG_BT_SETTINGS`.
   How to use this callback is shown later in this guide.
   For more information about persistent storage, see :ref:`zephyr:settings_api`.

:c:member:`bt_mesh_model_cb.start`
   This handler gets called after the node has been provisioned, or after all mesh data has been loaded from persistent storage.
   When this callback fires, the mesh model may start its behavior, and all Access APIs are ready for use.
   How to use this callback is shown later in this guide.

:c:member:`bt_mesh_model_cb.init`
   This handler is called on every model instance during mesh initialization.
   Implement it, if you need to do additional model initialization before the mesh stack starts, e.g. to initialize the model publication context.
   If any of the model init callbacks return an error, the Mesh subsystem initialization will be aborted, and the error will be returned to the caller of :c:func:`bt_mesh_init`.
   How to use this callback is shown later in this guide.

:c:member:`bt_mesh_model_cb.reset`
   The model reset handler is called when the mesh node is reset.
   All model data is deleted on reset, and the model should clear its state.
   If the model stores any persistent data, this needs to be erased manually.
   How to use this callback is shown later in this guide.

If you want to use any of these callbacks, create an instance of :c:struct:`bt_mesh_model_cb` and initialize any of the required callbacks.
Use :c:macro:`BT_MESH_MODEL_VND_CB` to associate the callbacks with your model.
It will initialize :c:member:`bt_mesh_model.cb` field of the model context.

Getting all things together
***************************

Now, after covering the basics of the model implementation, we can see how it works on the example of :ref:`bluetooth_mesh_chat` sample.
It implements a vendor model that is called the :ref:`bluetooth_mesh_chat_client_model`.

In the code below, you can see how Company ID, Model ID and messages opcodes for the :ref:`messages <bluetooth_mesh_chat_client_model_messages>` defined in the Chat Client model:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/include/chat_cli.h
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_1
   :end-before: include_endpoint_chat_cli_rst_1

The next snippet shows the opcode list for the messages defined in the :ref:`bluetooth_mesh_chat_client_model_messages`:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_2
   :end-before: include_endpoint_chat_cli_rst_2

To send :ref:`presence <bluetooth_mesh_chat_client_model_presence>` and non-private messages, the Chat Client model uses the model publication context.
This will let the Configuration Client configure an address where the messages will be published to.
Therefore, the model needs to declare :c:struct:`bt_mesh_model_pub`, :c:struct:`net_buf_simple` and a buffer for a message to be published.
In addition to that, the model needs to store a pointer to :c:struct:`bt_mesh_model`, to use the Access API.

All these parameters are unique for each model instance.
Therefore, together with other model specific parameters, they can be enclosed into a structure defining the model context:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/include/chat_cli.h
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_3
   :end-before: include_endpoint_chat_cli_rst_3

To initialize the model publication context and store :c:struct:`bt_mesh_model`, the model uses the :c:member:`bt_mesh_model_cb.init` handler:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_4
   :end-before: include_endpoint_chat_cli_rst_4

The model implements the :c:member:`bt_mesh_model_cb.start` handler to notify a user that the mesh data has been loaded from persistent storage and when the node is provisioned:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_5
   :end-before: include_endpoint_chat_cli_rst_5

The presence value of the model needs to be stored in persistent storage so that it can be restored at the board reboot.
The :c:func:`bt_mesh_model_data_store` is called to store the presence value:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_8
   :end-before: include_endpoint_chat_cli_rst_8

To restore the presence value, the model implements the :c:member:`bt_mesh_model_cb.settings_set` handler:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_3
   :end-before: include_endpoint_chat_cli_rst_3

The stored presence value needs to be reset if the node reset is applied.
This will make the model start as it was just initialized.
This is done through the :c:member:`bt_mesh_model_cb.reset` handler:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_6
   :end-before: include_endpoint_chat_cli_rst_6

Note that both callbacks as well as :c:func:`bt_mesh_model_data_store` call are wrapped into :option:`CONFIG_BT_SETTINGS`.
This let's exclude this functionality if the persistent storage is not enabled.

Here you can see initialization of the model callbacks structure:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_7
   :end-before: include_endpoint_chat_cli_rst_7

Now we have everything to define an entry for the node composition data.
For the convenience, the initialization of :c:macro:`BT_MESH_MODEL_VND_CB` macro is wrapped into a `BT_MESH_MODEL_CHAT_CLI` macro, which only requires a pointer to the instance of the model context defined above:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/include/chat_cli.h
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_2
   :end-before: include_endpoint_chat_cli_rst_2

Note that the user data, which stores a pointer to the model context, is initialized through :c:macro:`BT_MESH_MODEL_USER_DATA`.
This is done in order to ensure that the correct data type is passed to `BT_MESH_MODEL_CHAT_CLI`.

Here you can see how the defined macro is used when defining the node composition data:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/model_handler.c
   :language: c
   :start-after: include_startingpoint_model_handler_rst_1
   :end-before: include_endpoint_model_handler_rst_1

The Chat Client model allows to send private messages.
This means that only the node with the specified address will receive the message.
This is done by initializing :c:struct:`bt_mesh_msg_ctx` with a custom parameters:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_9
   :end-before: include_endpoint_chat_cli_rst_9

When the Chat Client model receives a private text message or some of the nodes wants to get the current presence of the model, it needs to send an acknowledgment back to the originator of the message.
This is done by calling :c:func:`bt_mesh_model_send` with the :c:struct:`bt_mesh_msg_ctx` passed to the message handler:

.. literalinclude:: ../../samples/bluetooth/mesh/chat/src/chat_cli.c
   :language: c
   :start-after: include_startingpoint_chat_cli_rst_1
   :end-before: include_endpoint_chat_cli_rst_1
