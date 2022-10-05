.. _ug_bt_mesh_fota:

Performing FOTA updates in Bluetooth mesh
#########################################

In |NCS|, there is currently no support for Device Firmware Update (DFU) over Bluetooth® mesh when performing firmware over-the-air (FOTA) updates for your Bluetooth mesh devices and applications.
However, support for FOTA updates with point-to-point DFU over Bluetooth Low Energy using the MCUmgr subsystem and the Simple Management Protocol (SMP) is available.

Following the instructions described in :ref:`FOTA over Bluetooth Low Energy<ug_nrf52_developing_ble_fota>`, you can enable the support for and perform FOTA updates using a mobile app.

If the device's composition data is going to change after the FOTA update on a Bluetooth mesh device is performed, unprovision the device before downloading the new image.

If you are using the `nRF Connect Device Manager`_ mobile app to perform FOTA updates, your Bluetooth mesh device might not be visible in the list of available devices.
This happens if the device is not advertising the SMP service UUID and the filter that only shows devices advertising this service is enabled.
The device can still be discovered through a service discovery, for example using the `nRF Connect for Mobile`_ app.
See `Discovering Bluetooth mesh devices in nRF Connect Device Manager`_ for more details.

FOTA in Bluetooth mesh samples
******************************

The :ref:`bluetooth_mesh_light` sample enables support for point-to-point DFU over Bluetooth Low Energy for nRF52 Series development kits.
See the sample documentation for more details.

Point-to-point DFU over Bluetooth Low Energy is supported by default, out-of-the-box, for all samples and applications compatible with :ref:`zephyr:thingy53_nrf5340`.
See :ref:`thingy53_app_update` for more information about updating firmware image on :ref:`zephyr:thingy53_nrf5340`.
For full list of samples and applications supported on :ref:`zephyr:thingy53_nrf5340`, see :ref:`thingy53_compatible_applications`.

.. note::
   If you are using the `nRF Connect Device Manager`_ mobile app to perform FOTA updates on :ref:`zephyr:thingy53_nrf5340`, your Bluetooth mesh device might not be visible in the list of available devices.

Discovering Bluetooth mesh devices in nRF Connect Device Manager
****************************************************************

To make sure your device is visible in the `nRF Connect Device Manager`_ mobile app, do one of the following:

* Disable the filter that only shows devices advertising the SMP service UUID in the `nRF Connect Device Manager`_ mobile app.
* Make the device advertise the SMP service UUID, and thus be discoverable by the `nRF Connect Device Manager`_ mobile app with the filter enabled.

Disabling the filter
====================

To disable the filter in the `nRF Connect Device Manager`_ mobile app, do the following steps:

1. Tap the :guilabel:`Filter` button at the right top corner of your screen.
#. Deselect :guilabel:`Only devices advertising SMP UUID`.

You should see the device appear in the list of devices.

Advertising SMP UUID
====================

To make sure that your Bluetooth mesh device advertises the SMP service UUID, you can use the :ref:`bluetooth_mesh_light` sample where this is enabled through configuration overlay file :file:`overlay-dfu.conf`.
Alternatively, do the following:

1. Configure the extended advertising API for your Bluetooth mesh sample by adding the following to the :file:`prj.conf` file::

    CONFIG_BT_EXT_ADV_MAX_ADV_SET=2 # 1 set for mesh, 1 set for advertising SMP server
    CONFIG_BT_MAX_CONN=2 # 1 conn for GATT Proxy/PB-GATT, 1 conn for SMP

#. Use the code from the :ref:`zephyr:smp_svr_sample` sample to advertise SMP UUID.

This will make the device discoverable by the `nRF Connect Device Manager`_ mobile app with the :guilabel:`Only devices advertising SMP UUID` filter enabled.
Observe that the device appears in the list of devices in the mobile app.
