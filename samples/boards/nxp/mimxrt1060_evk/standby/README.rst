.. zephyr:code-sample:: mimxrt1060_evk_standby
   :name: RT1060 System Off
   :relevant-api: sys_poweroff

   Use standby on MIMXRT1060-EVK.

Overview
********

This sample can be used for basic power measurement and as an example of
standby on NXP i.MX RT platforms. The functional behavior is:

* Busy-wait for 2 seconds
* Idle-wait for 2 seconds
* Enter standby after enabling wakeup through a button press, and
  additionally set an alarm 10 seconds in the future to wake up the processor

A power monitor will be able to distinguish among these states.

Requirements
************

This application uses MIMXRT1060_EVK or MIMXRT1060_EVKB for the demo.

Building, Flashing and Running
******************************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt1060_evk/standby
   :board: mimxrt1060_evk
   :goals: build flash
   :compact:

Running:

1. Open UART terminal.
2. Power Cycle Device.
3. Device will turn on and idle for 2 seconds
4. Device will enter standby state.
   Press SW 5 to wake the device and restart the application
   as if it had been powered back on.
   Alternatively, wait 10 seconds for the alarm to fire
   and wake the device up automatically.

Sample Output
=================
MIMXRT1060_EVK core output
--------------------------

.. code-block:: console

   *** Booting Zephyr OS build zephyr-v3.0.0-2733-ged206aca47cc  ***

   mimxrt1060_evkb system off demo
   Busy-wait 2 s
   Sleep 2 s
   RTC Alarm set for 10 seconds to wake from soft-off.
   Entering standby; press GPIO_5 to restart sooner
