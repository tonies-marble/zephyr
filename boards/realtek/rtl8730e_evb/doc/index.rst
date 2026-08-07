.. zephyr:board:: rtl8730e_evb

Overview
********

The Realtek RTL8730E is an AmebaSmart series SoC integrating a Cortex-A32
application processor ("AP" / "CA32"), a Cortex-M55 HP MCU ("HP" / "KM4") and
a Cortex-M0 LP MCU ("LP" / "KM0") alongside Wi-Fi 6 and BLE 5.2. The Zephyr
port exposes two build targets, selected by the cpucluster qualifier:

* ``rtl8730e_evb/rtl8730e/hp`` — the Cortex-M55 HP core. This is the
  currently functional target: it runs image2 XIP from flash and uses
  the external DRAM as its main RAM region.
* ``rtl8730e_evb/rtl8730e/ap`` — the Cortex-A32 AP core. Skeleton only;
  present so the port shape mirrors imx93_evk/mimx9352.

The LP core is not a Zephyr target — it stays as a prebuilt nuwa_lib blob,
which is also responsible for DDR/PSRAM bring-up before image2 starts.

Hardware
********

For details, refer to the vendor documentation for the RTL8730E.

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

Binary Blobs
============

The Realtek HAL requires binary blobs to boot the non-Zephyr cores. Fetch them
with:

.. code-block:: console

   west blobs fetch hal_realtek

.. note::

   It is recommended running the command above after ``west update``.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Building
========

An example of building the :zephyr:code-sample:`hello_world` application is
shown below:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: rtl8730e_evb/rtl8730e/hp
   :goals: build

Flashing
========

The standard ``flash`` target is supported. Put the SoC into download mode via
the on-board USB-UART converter before flashing, then reset into normal mode
once flashing has completed.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: rtl8730e_evb/rtl8730e/hp
   :goals: flash
   :flash-args: --port <port_name>

Debugging
=========

Debugging is supported via SWD using a J-Link probe attached to the HP-core
SWD lines. The default J-Link script selects CoreSight AP2 (KM4 AHB-AP).

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: rtl8730e_evb/rtl8730e/hp
   :maybe-skip-config:
   :goals: debug
